#include <avengine/search/index_shard_source.h>

#include <avengine/search/search_common.h>

#include <algorithm>
#include <cstdio>
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

uint64_t readU64LE(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(ptr[i]) << (i * 8);
  return value;
}

uint32_t readVarU32(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  uint32_t result = 0;
  uint32_t shift = 0;

  while (begin < end && shift < 35)
  {
    const uint8_t byte = *begin++;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u)
    {
      *next = begin;
      return result;
    }
    shift += 7;
  }

  *next = nullptr;
  return 0;
}

size_t cstrLenSafe(const uint8_t* start, const uint8_t* end)
{
  const uint8_t* cursor = start;
  while (cursor < end && *cursor != 0)
    ++cursor;
  if (cursor == end)
    return string::npos;
  return static_cast<size_t>(cursor - start);
}

string readLengthPrefixedString(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  const uint8_t* afterLength = nullptr;
  const uint32_t length = readVarU32(begin, end, &afterLength);
  if (!afterLength || afterLength + length > end)
  {
    *next = nullptr;
    return string();
  }

  string out;
  for (uint32_t i = 0; i < length; ++i)
    out.pushBack(static_cast<char>(afterLength[i]));
  *next = afterLength + length;
  return out;
}

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
  if (fstat(fd, &st) != 0 || st.st_size < 16)
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
  const uint64_t uniqueWordsPlusOne = readU64LE(data_ + 8);
  if (uniqueWordsPlusOne == 0 || !ParseDictionary(uniqueWordsPlusOne) || !ParseDocumentMetadata())
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

ISRWord* IndexShardSource::OpenTermIsr(const string& term) const
{
  const uint8_t* postingBegin = nullptr;
  const uint8_t* postingEnd = nullptr;
  uint64_t documentFrequency = 0;
  if (!FindPostingBounds(term, &postingBegin, &postingEnd, &documentFrequency))
    return nullptr;

  PostingBlobHeader header = {};
  const uint8_t* cursor = nullptr;
  if (!parsePostingBlobHeader(postingBegin, postingEnd, &header, &cursor))
    return nullptr;

  // This keeps the shard loader cheap: we only decode the posting lists the query actually asks
  // for, and we leave any smarter caching for the next slice.
  for (uint64_t i = 0; i < header.skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
    {
      const uint8_t* next = nullptr;
      readVarU32(cursor, postingEnd, &next);
      if (!next)
        return nullptr;
      cursor = next;
    }
  }

  vector<Location> positions;
  uint32_t absolutePosition = 0;
  for (uint64_t i = 0; i < header.collectionFrequency; ++i)
  {
    const uint8_t* next = nullptr;
    absolutePosition += readVarU32(cursor, postingEnd, &next);
    if (!next)
      return nullptr;
    cursor = next;
    positions.pushBack(static_cast<Location>(absolutePosition + 1u));
  }

  return new ISRWord(positions, documents_, static_cast<uint32_t>(documentFrequency));
}

ISREndDoc* IndexShardSource::OpenEndDocIsr() const
{
  if (documents_.empty())
    return nullptr;
  return new ISREndDoc(documents_);
}

bool IndexShardSource::ParseDictionary(uint64_t uniqueWordsPlusOne)
{
  const uint64_t tableSlots = uniqueWordsPlusOne * 2u;
  const size_t dictionaryBytes = static_cast<size_t>(16u + (tableSlots * 16u));
  if (dictionaryBytes > fileSize_)
    return false;

  const uint8_t* stringStart = data_ + dictionaryBytes;
  const uint8_t* fileEnd = data_ + fileSize_;
  const size_t expectedTerms = static_cast<size_t>(uniqueWordsPlusOne - 1u);
  size_t inferredStringBytes = 0;

  for (uint64_t slot = 0; slot < tableSlots; ++slot)
  {
    const size_t offset = static_cast<size_t>(16u + (slot * 16u));
    const DictEntry entry = {readU64LE(data_ + offset), readU64LE(data_ + offset + 8u)};
    if (entry.stringOffset == 0 && entry.postingOffset == 0)
      continue;
    if (dictionaryBytes + entry.stringOffset >= fileSize_)
      continue;

    const uint8_t* termPtr = stringStart + entry.stringOffset;
    const size_t length = cstrLenSafe(termPtr, fileEnd);
    if (length == string::npos)
      continue;

    terms_.pushBack({string(reinterpret_cast<const char*>(termPtr)), entry});
    inferredStringBytes =
        std::max(inferredStringBytes, static_cast<size_t>(entry.stringOffset) + length + 1u);
  }

  if (terms_.size() + 1u == expectedTerms)
  {
    // The writer still lets one real term collide with the all-zero empty-slot sentinel.
    // If we are missing exactly one term, treat offset 0 / posting 0 as the cheap recovery path.
    const size_t length = cstrLenSafe(stringStart, fileEnd);
    if (length != string::npos)
    {
      terms_.pushBack({string(reinterpret_cast<const char*>(stringStart)), {0, 0}});
      inferredStringBytes = std::max(inferredStringBytes, length + 1u);
    }
  }

  postingsStart_ = dictionaryBytes + inferredStringBytes;
  return postingsStart_ <= fileSize_;
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

  vector<Location> boundaries;
  for (uint64_t i = 0; i < header.skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
    {
      const uint8_t* next = nullptr;
      readVarU32(cursor, postingEnd, &next);
      if (!next)
        return false;
      cursor = next;
    }
  }

  uint32_t absolutePosition = 0;
  for (uint64_t i = 0; i < header.collectionFrequency; ++i)
  {
    const uint8_t* next = nullptr;
    absolutePosition += readVarU32(cursor, postingEnd, &next);
    if (!next)
      return false;
    cursor = next;
    boundaries.pushBack(static_cast<Location>(absolutePosition));
  }

  Location start = 1;
  for (uint64_t docId = 0; docId < documentFrequency; ++docId)
  {
    const uint8_t* next = nullptr;
    const string url = readLengthPrefixedString(cursor, postingEnd, &next);
    if (!next)
      return false;
    cursor = next;

    const string title = readLengthPrefixedString(cursor, postingEnd, &next);
    if (!next || next + sizeof(uint64_t) > postingEnd)
      return false;
    cursor = next + sizeof(uint64_t);

    const Location end = docId < boundaries.size() ? boundaries[static_cast<size_t>(docId)] : start;
    documents_.pushBack({static_cast<uint32_t>(docId), start, end,
                         static_cast<uint32_t>(title.size()), static_cast<uint32_t>(url.size())});
    documentUrls_.pushBack(url);
    documentTitles_.pushBack(title);
    start = end + 1;
  }

  return !documents_.empty();
}

bool IndexShardSource::FindPostingBounds(const string& term, const uint8_t** begin,
                                         const uint8_t** end, uint64_t* documentFrequency) const
{
  const ParsedTerm* found = nullptr;
  for (size_t i = 0; i < terms_.size(); ++i)
  {
    if (terms_[i].Term == term)
      found = &terms_[i];
  }

  if (!found)
    return false;

  uint64_t nextPostingOffset = static_cast<uint64_t>(fileSize_ - postingsStart_);
  for (size_t i = 0; i < terms_.size(); ++i)
  {
    if (terms_[i].Entry.postingOffset > found->Entry.postingOffset &&
        terms_[i].Entry.postingOffset < nextPostingOffset)
    {
      nextPostingOffset = terms_[i].Entry.postingOffset;
    }
  }

  const size_t startOffset = postingsStart_ + static_cast<size_t>(found->Entry.postingOffset);
  const size_t endOffset = postingsStart_ + static_cast<size_t>(nextPostingOffset);
  if (startOffset + 24u > endOffset || endOffset > fileSize_)
    return false;

  if (documentFrequency)
    *documentFrequency = readU64LE(data_ + startOffset);
  *begin = data_ + startOffset;
  *end = data_ + endOffset;
  return true;
}

void IndexShardSource::Reset()
{
  const size_t mappedSize = fileSize_;
  terms_.clear();
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
