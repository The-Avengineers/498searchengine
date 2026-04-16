#include "avengine/indexer/indexer.h"
#include <avengine/codec.h>
#include <avengine/search/index_shard_source.h>

#include <avengine/avengine_lib/string_slice.h>
#include <avengine/search/search_common.h>

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

  // Tell the kernel not to prefetch sequential pages — index access patterns
  // are random (only query terms are touched), so read-ahead wastes RAM.
  madvise(data_, fileSize_, MADV_RANDOM);

  Reader headerReader(data_, fileSize_);
  const uint64_t magic = headerReader.readU64();
  (void)headerReader.readU64(); // document_count
  const uint64_t uniqueWords = headerReader.readU64();
  (void)headerReader.readU64();                       // token_offset
  uint64_t rankingInputSize = headerReader.readU64(); // ranking-input size

  if (rankingInputSize != sizeof(RankingInput))
  {
    Reset();
    return false;
  }

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
  if (docId >= docUrls_.size())
    return string();
  const DocSlice& s = docUrls_[docId];
  return string_slice(reinterpret_cast<char*>(data_ + s.offset), s.len).toString();
}

string IndexShardSource::GetDocumentTitle(uint32_t docId) const
{
  if (docId >= docTitles_.size())
    return string();
  const DocSlice& s = docTitles_[docId];
  return string_slice(reinterpret_cast<char*>(data_ + s.offset), s.len).toString();
}

string IndexShardSource::GetDocumentDescription(uint32_t docId) const
{
  if (docId >= docDescriptions_.size())
    return string();
  const DocSlice& s = docDescriptions_[docId];
  return string_slice(reinterpret_cast<char*>(data_ + s.offset), s.len).toString();
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
  const uint8_t* postingBegin = nullptr;
  const uint8_t* postingEnd = nullptr;
  uint64_t documentFrequency = 0;
  if (!FindPostingBounds(term, &postingBegin, &postingEnd, &documentFrequency))
    return nullptr;

  PostingBlobHeader header = {};
  const uint8_t* fieldStreamsBegin = nullptr;
  if (!parsePostingBlobHeader(postingBegin, postingEnd, &header, &fieldStreamsBegin))
    return nullptr;

  // Parse the skip pointer table — O(sqrt(N)) entries.
  vector<SkipPointerEntry> skipPointers;
  Reader r(fieldStreamsBegin, static_cast<size_t>(postingEnd - fieldStreamsBegin));
  for (uint64_t i = 0; i < header.skipCount; ++i)
  {
    SkipPointerEntry entry;
    entry.positionIndex = r.readVarU32();
    entry.positionValue = r.readVarU32();
    entry.byteOffset = r.readVarU32();
    skipPointers.pushBack(entry);
  }

  // Delta stream starts immediately after the skip table.
  const uint8_t* deltaStreamStart = r.cursor();

  return new ISRWordLazy(deltaStreamStart, postingEnd, std::move(skipPointers),
                         header.collectionFrequency, documents_,
                         static_cast<uint32_t>(documentFrequency));
}

ISREndDoc* IndexShardSource::OpenEndDocIsr() const
{
  if (documents_.empty())
    return nullptr;
  return new ISREndDoc(documents_);
}

bool IndexShardSource::ParseDictionary(uint64_t uniqueWords)
{
  termIndex_.clear();
  if (uniqueWords == 0)
    return false;

  const size_t fixedHeaderBytes = 5u * sizeof(uint64_t);
  if (fileSize_ < fixedHeaderBytes)
    return false;

  Reader r(data_ + fixedHeaderBytes, fileSize_ - fixedHeaderBytes);

  // Collect {termOffset, termLen, rawPostingOffset} without copying term strings.
  struct RawEntry
  {
    uint32_t termOffset;
    uint32_t termLen;
    uint64_t postingOffset;
  };
  vector<RawEntry> raw;

  for (uint64_t i = 0; i < uniqueWords; ++i)
  {
    if (r.remaining() < 8)
      return false;
    const uint64_t termLen = r.readU64();
    const uint32_t termOffset = static_cast<uint32_t>(r.cursor() - data_);
    r.skip(termLen);
    (void)r.readU64(); // positions_count
    (void)r.readU64(); // doc/postings_count
    const uint64_t postingOffset = r.readU64();
    raw.pushBack({termOffset, static_cast<uint32_t>(termLen), postingOffset});
  }

  postingsStart_ = static_cast<size_t>(r.cursor() - data_);
  if (postingsStart_ > fileSize_)
    return false;
  const size_t postingsBytes = fileSize_ - postingsStart_;

  // Sort by postingOffset to compute contiguous span end offsets.
  raw.sort([](const RawEntry& a, const RawEntry& b) {
    return a.postingOffset < b.postingOffset;
  });

  termIndex_.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i)
  {
    const size_t start = postingsStart_ + static_cast<size_t>(raw[i].postingOffset);
    const size_t end = i + 1 < raw.size()
        ? postingsStart_ + static_cast<size_t>(raw[i + 1].postingOffset)
        : postingsStart_ + postingsBytes;
    if (start > end || end > fileSize_)
      return false;
    termIndex_.pushBack({raw[i].termOffset, raw[i].termLen, start, end});
  }

  // Sort by term string for binary search at query time.
  termIndex_.sort([this](const TermIndex& a, const TermIndex& b) {
    const string_slice sa(reinterpret_cast<char*>(data_ + a.termOffset), a.termLen);
    const string_slice sb(reinterpret_cast<char*>(data_ + b.termOffset), b.termLen);
    return sa < sb;
  });

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
    const uint64_t urlLen = r.readU64();
    const uint32_t urlOff = static_cast<uint32_t>(r.cursor() - data_);
    r.skip(urlLen);

    const uint64_t titleLen = r.readU64();
    const uint32_t titleOff = static_cast<uint32_t>(r.cursor() - data_);
    r.skip(titleLen);

    const uint64_t descLen = r.readU64();
    const uint32_t descOff = static_cast<uint32_t>(r.cursor() - data_);
    r.skip(descLen);

    RankingInput rankingInput = {};
    if (r.remaining() >= sizeof(RankingInput))
      r.readBytes(&rankingInput, sizeof(RankingInput));
    const Location end = docId < boundaries.size() ? boundaries[static_cast<size_t>(docId)] : start;
    documents_.pushBack(
        {static_cast<uint32_t>(docId), start, end,
         static_cast<uint32_t>(titleLen), static_cast<uint32_t>(urlLen), rankingInput});
    docUrls_.pushBack({urlOff, static_cast<uint32_t>(urlLen)});
    docTitles_.pushBack({titleOff, static_cast<uint32_t>(titleLen)});
    docDescriptions_.pushBack({descOff, static_cast<uint32_t>(descLen)});
    start = end + 1;
  }

  return !documents_.empty();
}

bool IndexShardSource::FindPostingBounds(const string& term, const uint8_t** begin,
                                         const uint8_t** end, uint64_t* documentFrequency) const
{
  const string_slice query(term.cstr()); // null-terminated constructor
  size_t lo = 0, hi = termIndex_.size();
  while (lo < hi)
  {
    const size_t mid = lo + (hi - lo) / 2;
    const TermIndex& t = termIndex_[mid];
    const string_slice midTerm(reinterpret_cast<char*>(data_ + t.termOffset), t.termLen);
    if (midTerm < query)
      lo = mid + 1;
    else if (query < midTerm)
      hi = mid;
    else
    {
      if (t.startOffset + 24u > t.endOffset || t.endOffset > fileSize_)
        return false;
      if (documentFrequency)
        *documentFrequency = Reader(data_ + t.startOffset, 8u).readU64();
      *begin = data_ + t.startOffset;
      *end = data_ + t.endOffset;
      return true;
    }
  }
  return false;
}

void IndexShardSource::Reset()
{
  const size_t mappedSize = fileSize_;
  termIndex_.clear();
  documents_.clear();
  docUrls_.clear();
  docTitles_.clear();
  docDescriptions_.clear();
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
