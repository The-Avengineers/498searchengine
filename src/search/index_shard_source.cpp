#include <avengine/codec.h>
#include <avengine/search/index_shard_source.h>

#include <avengine/search/search_common.h>

#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace avengine
{
namespace search
{
namespace
{

constexpr uint64_t kIndexMagic = 0x008FB8EF84979FF0ULL;

} // namespace

IndexShardSource::IndexShardSource(const string& path)
{
  Load(path);
}

IndexShardSource::~IndexShardSource()
{
  Reset();
}

bool IndexShardSource::Load(const string& path)
{
  Reset();
  path_ = path;

  const int fd = open(path.cstr(), O_RDONLY);
  if (fd < 0)
    return false;

  struct stat st = {};
  if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(5u * sizeof(uint64_t)))
  {
    close(fd);
    return false;
  }

  data_ = static_cast<uint8_t*>(
      mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (data_ == MAP_FAILED)
  {
    data_ = nullptr;
    return false;
  }

  fileSize_ = static_cast<size_t>(st.st_size);

  Reader headerReader(data_, fileSize_);
  const uint64_t magic = headerReader.readU64();
  (void)headerReader.readU64(); // document_count
  const uint64_t uniqueWords = headerReader.readU64();
  (void)headerReader.readU64(); // token_offset
  (void)headerReader.readU64(); // ranking-input size

  if (magic != kIndexMagic || !ParseDictionary(uniqueWords) || !ParseDocumentMetadata())
  {
    Reset();
    return false;
  }

  return true;
}

bool IndexShardSource::IsLoaded() const
{
  return data_ != nullptr;
}

string IndexShardSource::GetPath() const
{
  return path_;
}

string IndexShardSource::GetDocumentURL(uint32_t docId) const
{
  if (docId >= documentUrls_.size())
    return string();
  return documentUrls_[docId];
}

string IndexShardSource::GetDocumentTitle(uint32_t docId) const
{
  if (docId >= documentTitles_.size())
    return string();
  return documentTitles_[docId];
}

const DocumentInfo* IndexShardSource::GetDocumentInfo(uint32_t docId) const
{
  if (docId >= documents_.size())
    return nullptr;
  return &documents_[docId];
}

bool IndexShardSource::DecodeTermOccurrences(const string& term, vector<Location>* positions,
                                             uint32_t* documentFrequency) const
{
  if (!positions || !documentFrequency)
    return false;

  positions->clear();
  *documentFrequency = 0;

  const uint8_t* postingBegin = nullptr;
  const uint8_t* postingEnd = nullptr;
  uint64_t decodedDocumentFrequency = 0;
  if (!FindPostingBounds(term, &postingBegin, &postingEnd, &decodedDocumentFrequency))
    return false;

  PostingBlobHeader header = {};
  const uint8_t* cursor = nullptr;
  if (!parsePostingBlobHeader(postingBegin, postingEnd, &header, &cursor))
    return false;

  Reader r(cursor, static_cast<size_t>(postingEnd - cursor));
  for (uint64_t i = 0; i < header.skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
      r.readVarU32();
  }

  uint32_t absolutePosition = 0;
  for (uint64_t i = 0; i < header.collectionFrequency; ++i)
  {
    const uint32_t delta = r.readVarU32();
    absolutePosition += delta;
    positions->pushBack(static_cast<Location>(absolutePosition + 1u));
  }

  *documentFrequency = static_cast<uint32_t>(decodedDocumentFrequency);
  return true;
}

ISRWord* IndexShardSource::BuildWordIsr(const vector<Location>& positions,
                                        uint32_t documentFrequency) const
{
  return new ISRWord(positions, documents_, documentFrequency);
}

ISRWord* IndexShardSource::OpenTermIsr(const string& term) const
{
  vector<Location> positions;
  uint32_t documentFrequency = 0;
  if (!DecodeTermOccurrences(term, &positions, &documentFrequency))
    return nullptr;
  return BuildWordIsr(positions, documentFrequency);
}

ISREndDoc* IndexShardSource::OpenEndDocIsr() const
{
  if (documents_.empty())
    return nullptr;
  return new ISREndDoc(documents_);
}

bool IndexShardSource::ParseDictionary(uint64_t uniqueWords)
{
  terms_.clear();
  if (uniqueWords == 0)
    return false;

  const size_t fixedHeaderBytes = 5u * sizeof(uint64_t);
  if (fileSize_ < fixedHeaderBytes)
    return false;

  Reader r(data_ + fixedHeaderBytes, fileSize_ - fixedHeaderBytes);
  for (uint64_t i = 0; i < uniqueWords; ++i)
  {
    const string term = r.readString();
    (void)r.readU64(); // positions_count
    (void)r.readU64(); // doc/postings_count
    const uint64_t postingOffset = r.readU64();
    terms_.pushBack({term, {postingOffset}});
  }

  postingsStart_ = static_cast<size_t>(r.cursor() - data_);
  if (postingsStart_ > fileSize_)
    return false;

  vector<const ParsedTerm*> sortedTerms;
  for (size_t i = 0; i < terms_.size(); ++i)
    sortedTerms.pushBack(&terms_[i]);

  sortedTerms.sort([](const ParsedTerm* lhs, const ParsedTerm* rhs)
                   { return lhs->Entry.postingOffset < rhs->Entry.postingOffset; });

  unordered_map<PostingSpan> postingBounds(sortedTerms.size() * 2u + 1u);
  const size_t postingsBytes = fileSize_ - postingsStart_;
  for (size_t i = 0; i < sortedTerms.size(); ++i)
  {
    const ParsedTerm* current = sortedTerms[i];
    const size_t startOffset = postingsStart_ + static_cast<size_t>(current->Entry.postingOffset);
    const size_t endOffset =
        i + 1 < sortedTerms.size()
            ? postingsStart_ + static_cast<size_t>(sortedTerms[i + 1]->Entry.postingOffset)
            : postingsStart_ + postingsBytes;

    if (startOffset > endOffset || endOffset > fileSize_)
      return false;

    postingBounds.insert(current->Term, {startOffset, endOffset});
  }
  swap(postingBounds_, postingBounds);

  return true;
}

bool IndexShardSource::ParseDocumentMetadata()
{
  const uint8_t* postingBegin = nullptr;
  const uint8_t* postingEnd = nullptr;
  uint64_t documentFrequency = 0;
  if (!FindPostingBounds(string("DOCUMENT_END"), &postingBegin, &postingEnd, &documentFrequency))
    return false;

  PostingBlobHeader header = {};
  const uint8_t* cursor = nullptr;
  if (!parsePostingBlobHeader(postingBegin, postingEnd, &header, &cursor))
    return false;

  Reader r(cursor, static_cast<size_t>(postingEnd - cursor));
  vector<Location> boundaries;
  for (uint64_t i = 0; i < header.skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
      r.readVarU32();
  }

  uint32_t absolutePosition = 0;
  for (uint64_t i = 0; i < header.collectionFrequency; ++i)
  {
    const uint32_t delta = r.readVarU32();
    absolutePosition += delta;
    boundaries.pushBack(static_cast<Location>(absolutePosition));
  }

  Location start = 1;
  for (uint64_t docId = 0; docId < documentFrequency; ++docId)
  {
    const avengine::string url = r.readString();
    const avengine::string title = r.readString();
    RankingInput rankingInput = 0;
    if (r.remaining() >= sizeof(uint64_t))
      rankingInput = r.readU64();

    const Location end = docId < boundaries.size() ? boundaries[static_cast<size_t>(docId)] : start;
    const uint32_t titleLength = static_cast<uint32_t>(title.size());
    const uint32_t urlLength = static_cast<uint32_t>(url.size());
    documents_.pushBack(
        {static_cast<uint32_t>(docId), start, end, titleLength, urlLength, rankingInput});
    documentUrls_.pushBack(url);
    documentTitles_.pushBack(title);
    start = end + 1;
  }

  return !documents_.empty();
}

bool IndexShardSource::FindPostingBounds(const string& term, const uint8_t** begin,
                                         const uint8_t** end, uint64_t* documentFrequency) const
{
  const PostingSpan* span = postingBounds_.get(string_slice(term.cstr()));
  if (!span)
    return false;

  const size_t startOffset = span->StartOffset;
  const size_t endOffset = span->EndOffset;
  if (startOffset + 24u > endOffset || endOffset > fileSize_)
    return false;

  if (documentFrequency)
    *documentFrequency = Reader(data_ + startOffset, 8u).readU64();

  *begin = data_ + startOffset;
  *end = data_ + endOffset;
  return true;
}

void IndexShardSource::Reset()
{
  const size_t mappedSize = fileSize_;
  terms_.clear();
  unordered_map<PostingSpan> emptyPostingBounds;
  swap(postingBounds_, emptyPostingBounds);
  documents_.clear();
  documentUrls_.clear();
  documentTitles_.clear();
  postingsStart_ = 0;
  fileSize_ = 0;

  if (data_)
  {
    munmap(data_, mappedSize);
    data_ = nullptr;
  }
}

} // namespace search
} // namespace avengine
