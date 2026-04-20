#include <avengine/search/legacy_shard_reader.h>

#include <cerrno>
#include <cstring>
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

constexpr uint64_t kLegacyIndexMagic = 0x008FB8EF84979FF0ULL;

bool RangeInFile(size_t offset, size_t length, size_t fileSize)
{
  return offset <= fileSize && length <= fileSize - offset;
}

bool ReadU64Le(const uint8_t* data, size_t fileSize, size_t* cursor, uint64_t* out)
{
  if (!cursor || !out || !RangeInFile(*cursor, sizeof(uint64_t), fileSize))
    return false;

  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(uint64_t); ++i)
    value |= static_cast<uint64_t>(data[*cursor + i]) << (i * 8u);
  *cursor += sizeof(uint64_t);
  *out = value;
  return true;
}

bool ReadVarU32(const uint8_t* data, size_t fileSize, size_t* cursor, size_t end, uint32_t* out)
{
  if (!cursor || !out || *cursor > end || end > fileSize)
    return false;

  uint32_t value = 0;
  uint32_t shift = 0;
  while (*cursor < end && shift < 35)
  {
    const uint8_t byte = data[*cursor];
    ++(*cursor);
    value |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u)
    {
      *out = value;
      return true;
    }
    shift += 7u;
  }

  return false;
}

bool ReadString(const uint8_t* data, size_t fileSize, size_t* cursor, string* out)
{
  uint64_t length = 0;
  if (!ReadU64Le(data, fileSize, cursor, &length))
    return false;
  if (length > fileSize || !RangeInFile(*cursor, static_cast<size_t>(length), fileSize))
    return false;

  string value;
  for (uint64_t i = 0; i < length; ++i)
    value.pushBack(static_cast<char>(data[*cursor + static_cast<size_t>(i)]));
  *cursor += static_cast<size_t>(length);
  *out = value;
  return true;
}

bool DecodePostingHeader(const uint8_t* data, size_t fileSize, size_t start, size_t end,
                         uint64_t* documentFrequency, uint64_t* collectionFrequency,
                         uint64_t* skipCount, size_t* payloadCursor)
{
  if (!documentFrequency || !collectionFrequency || !skipCount || !payloadCursor ||
      start > end || end > fileSize)
  {
    return false;
  }

  size_t cursor = start;
  if (!ReadU64Le(data, fileSize, &cursor, documentFrequency) ||
      !ReadU64Le(data, fileSize, &cursor, collectionFrequency) ||
      !ReadU64Le(data, fileSize, &cursor, skipCount))
  {
    return false;
  }

  if (cursor > end)
    return false;
  *payloadCursor = cursor;
  return true;
}

} // namespace

LegacyShardReader::LegacyShardReader(const string& path)
{
  Load(path);
}

LegacyShardReader::~LegacyShardReader()
{
  Reset();
}

bool LegacyShardReader::Load(const string& path)
{
  Reset();
  path_ = path;

  const int fd = open(path.cstr(), O_RDONLY);
  if (fd < 0)
  {
    SetErrnoError("open", path);
    return false;
  }

  struct stat st = {};
  if (fstat(fd, &st) != 0)
  {
    SetErrnoError("fstat", path);
    close(fd);
    return false;
  }

  if (st.st_size <= 0)
  {
    SetError("empty shard");
    close(fd);
    return false;
  }

  fileSize_ = static_cast<size_t>(st.st_size);
  data_ = static_cast<uint8_t*>(mmap(nullptr, fileSize_, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);

  if (data_ == MAP_FAILED)
  {
    data_ = nullptr;
    SetErrnoError("mmap", path);
    return false;
  }

  uint64_t documentCount = 0;
  if (!ParseHeaderAndDictionary(&documentCount) || !ParseDocumentMetadata(documentCount))
  {
    Reset();
    return false;
  }

  loaded_ = true;
  return true;
}

void LegacyShardReader::Reset()
{
  if (data_)
  {
    munmap(data_, fileSize_);
    data_ = nullptr;
  }
  fileSize_ = 0;
  loaded_ = false;
  terms_.clear();
  documents_.clear();
}

bool LegacyShardReader::IsLoaded() const
{
  return loaded_;
}

string LegacyShardReader::GetError() const
{
  return error_;
}

size_t LegacyShardReader::TermCount() const
{
  return terms_.size();
}

size_t LegacyShardReader::DocumentCount() const
{
  return documents_.size();
}

const LegacyTermInfo* LegacyShardReader::GetTerm(size_t index) const
{
  if (index >= terms_.size())
    return nullptr;
  return &terms_[index].info;
}

const LegacyDocumentMetadata* LegacyShardReader::GetDocument(uint32_t docId) const
{
  if (static_cast<size_t>(docId) >= documents_.size())
    return nullptr;
  return &documents_[docId];
}

bool LegacyShardReader::DecodeTermDocumentStats(const string& term,
                                                vector<LegacyDocTermStats>* out) const
{
  if (!out)
    return false;
  out->clear();

  const TermEntry* entry = FindTerm(term);
  if (!entry)
    return false;

  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
  size_t cursor = 0;
  if (!DecodePostingHeader(data_, fileSize_, entry->postingStart, entry->postingEnd,
                           &documentFrequency, &collectionFrequency, &skipCount, &cursor))
  {
    return false;
  }

  for (uint64_t i = 0; i < skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
    {
      uint32_t ignored = 0;
      if (!ReadVarU32(data_, fileSize_, &cursor, entry->postingEnd, &ignored))
        return false;
    }
  }

  uint32_t absolutePosition = 0;
  size_t docIndex = 0;
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    uint32_t delta = 0;
    if (!ReadVarU32(data_, fileSize_, &cursor, entry->postingEnd, &delta))
      return false;
    absolutePosition += delta;
    const uint32_t location = absolutePosition + 1u;

    while (docIndex < documents_.size() && documents_[docIndex].endLocation < location)
      ++docIndex;
    if (docIndex >= documents_.size())
      return false;
    if (location < documents_[docIndex].startLocation)
      return false;

    const uint32_t docId = documents_[docIndex].docId;
    if (out->empty() || out->back().docId != docId)
      out->pushBack({docId, 1u});
    else
      out->back().termFrequency += 1u;
  }

  return out->size() == documentFrequency;
}

bool LegacyShardReader::DecodeTermDocumentPositions(const string& term,
                                                    vector<LegacyDocTermPositions>* out) const
{
  if (!out)
    return false;
  out->clear();

  const TermEntry* entry = FindTerm(term);
  if (!entry)
    return false;

  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
  size_t cursor = 0;
  if (!DecodePostingHeader(data_, fileSize_, entry->postingStart, entry->postingEnd,
                           &documentFrequency, &collectionFrequency, &skipCount, &cursor))
  {
    return false;
  }

  for (uint64_t i = 0; i < skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
    {
      uint32_t ignored = 0;
      if (!ReadVarU32(data_, fileSize_, &cursor, entry->postingEnd, &ignored))
        return false;
    }
  }

  uint32_t absolutePosition = 0;
  size_t docIndex = 0;
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    uint32_t delta = 0;
    if (!ReadVarU32(data_, fileSize_, &cursor, entry->postingEnd, &delta))
      return false;
    absolutePosition += delta;
    const uint32_t location = absolutePosition + 1u;

    while (docIndex < documents_.size() && documents_[docIndex].endLocation < location)
      ++docIndex;
    if (docIndex >= documents_.size())
      return false;
    if (location < documents_[docIndex].startLocation)
      return false;

    const uint32_t docId = documents_[docIndex].docId;
    if (out->empty() || out->back().docId != docId)
    {
      LegacyDocTermPositions positions;
      positions.docId = docId;
      out->pushBack(positions);
    }
    out->back().positions.pushBack(location);
  }

  return out->size() == documentFrequency;
}

bool LegacyShardReader::ParseHeaderAndDictionary(uint64_t* documentCount)
{
  if (!documentCount)
    return false;
  if (fileSize_ < 5u * sizeof(uint64_t))
  {
    SetError("truncated shard header");
    return false;
  }

  size_t cursor = 0;
  uint64_t magic = 0;
  uint64_t uniqueWords = 0;
  uint64_t ignored = 0;
  uint64_t rankingInputSize = 0;
  if (!ReadU64Le(data_, fileSize_, &cursor, &magic) ||
      !ReadU64Le(data_, fileSize_, &cursor, documentCount) ||
      !ReadU64Le(data_, fileSize_, &cursor, &uniqueWords) ||
      !ReadU64Le(data_, fileSize_, &cursor, &ignored) ||
      !ReadU64Le(data_, fileSize_, &cursor, &rankingInputSize))
  {
    SetError("invalid shard header");
    return false;
  }

  if (magic != kLegacyIndexMagic)
  {
    SetError("invalid shard magic");
    return false;
  }
  if (rankingInputSize != sizeof(RankingInput))
  {
    SetError("unsupported ranking input layout");
    return false;
  }

  struct RawTerm
  {
    string term;
    uint64_t positionsCount = 0;
    uint64_t dictionaryDocumentFrequency = 0;
    uint64_t postingOffset = 0;
  };

  vector<RawTerm> rawTerms;
  for (uint64_t i = 0; i < uniqueWords; ++i)
  {
    string term;
    uint64_t positionsCount = 0;
    uint64_t dictionaryDocumentFrequency = 0;
    uint64_t postingOffset = 0;
    if (!ReadString(data_, fileSize_, &cursor, &term) ||
        !ReadU64Le(data_, fileSize_, &cursor, &positionsCount) ||
        !ReadU64Le(data_, fileSize_, &cursor, &dictionaryDocumentFrequency) ||
        !ReadU64Le(data_, fileSize_, &cursor, &postingOffset))
    {
      SetError("truncated term dictionary");
      return false;
    }
    rawTerms.pushBack({term, positionsCount, dictionaryDocumentFrequency, postingOffset});
  }

  const size_t postingsStart = cursor;
  if (postingsStart > fileSize_)
  {
    SetError("invalid postings start");
    return false;
  }

  rawTerms.sort([](const RawTerm& a, const RawTerm& b) {
    return a.postingOffset < b.postingOffset;
  });

  vector<TermEntry> byPosting;
  for (size_t i = 0; i < rawTerms.size(); ++i)
  {
    const size_t start = postingsStart + static_cast<size_t>(rawTerms[i].postingOffset);
    const size_t end = i + 1 < rawTerms.size()
        ? postingsStart + static_cast<size_t>(rawTerms[i + 1].postingOffset)
        : fileSize_;
    if (start > end || end > fileSize_)
    {
      SetError("invalid posting bounds");
      return false;
    }

    uint64_t documentFrequency = 0;
    uint64_t collectionFrequency = 0;
    uint64_t skipCount = 0;
    size_t payloadCursor = 0;
    if (!DecodePostingHeader(data_, fileSize_, start, end, &documentFrequency,
                             &collectionFrequency, &skipCount, &payloadCursor))
    {
      SetError("truncated posting header");
      return false;
    }

    if (documentFrequency != rawTerms[i].dictionaryDocumentFrequency ||
        collectionFrequency != rawTerms[i].positionsCount)
    {
      SetError("term dictionary and posting header disagree");
      return false;
    }

    TermEntry entry;
    entry.info.term = rawTerms[i].term;
    entry.info.documentFrequency = documentFrequency;
    entry.info.collectionFrequency = collectionFrequency;
    entry.postingStart = start;
    entry.postingEnd = end;
    entry.skipCount = skipCount;
    byPosting.pushBack(entry);
  }

  byPosting.sort([](const TermEntry& a, const TermEntry& b) {
    return a.info.term < b.info.term;
  });
  terms_.clear();
  terms_.reserve(byPosting.size());
  for (const TermEntry& entry : byPosting)
    terms_.pushBack(entry);
  return true;
}

bool LegacyShardReader::ParseDocumentMetadata(uint64_t expectedDocumentCount)
{
  const TermEntry* docEnd = FindTerm(string(DOCEND_TOKEN));
  if (!docEnd)
  {
    SetError("DOCUMENT_END not found");
    return false;
  }

  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
  size_t cursor = 0;
  if (!DecodePostingHeader(data_, fileSize_, docEnd->postingStart, docEnd->postingEnd,
                           &documentFrequency, &collectionFrequency, &skipCount, &cursor))
  {
    SetError("invalid DOCUMENT_END posting");
    return false;
  }
  if (documentFrequency != expectedDocumentCount || collectionFrequency != expectedDocumentCount)
  {
    SetError("DOCUMENT_END count mismatch");
    return false;
  }

  for (uint64_t i = 0; i < skipCount; ++i)
  {
    for (int field = 0; field < 3; ++field)
    {
      uint32_t ignored = 0;
      if (!ReadVarU32(data_, fileSize_, &cursor, docEnd->postingEnd, &ignored))
      {
        SetError("truncated DOCUMENT_END skip data");
        return false;
      }
    }
  }

  vector<uint32_t> boundaries;
  uint32_t absolutePosition = 0;
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    uint32_t delta = 0;
    if (!ReadVarU32(data_, fileSize_, &cursor, docEnd->postingEnd, &delta))
    {
      SetError("truncated DOCUMENT_END positions");
      return false;
    }
    absolutePosition += delta;
    boundaries.pushBack(absolutePosition);
  }

  uint32_t start = 1;
  for (uint64_t docId = 0; docId < documentFrequency; ++docId)
  {
    string url;
    string title;
    string description;
    if (!ReadString(data_, fileSize_, &cursor, &url) ||
        !ReadString(data_, fileSize_, &cursor, &title) ||
        !ReadString(data_, fileSize_, &cursor, &description) ||
        !RangeInFile(cursor, sizeof(RankingInput), docEnd->postingEnd))
    {
      SetError("truncated DOCUMENT_END metadata");
      return false;
    }

    RankingInput rankingInput = {};
    std::memcpy(&rankingInput, data_ + cursor, sizeof(rankingInput));
    cursor += sizeof(rankingInput);

    const uint32_t end = boundaries[static_cast<size_t>(docId)];
    const uint32_t docLength = end >= start ? (end - start + 1u) : 0u;
    documents_.pushBack({static_cast<uint32_t>(docId), start, end, docLength, url, title,
                         description, rankingInput});
    start = end + 1u;
  }

  return documents_.size() == expectedDocumentCount;
}

const LegacyShardReader::TermEntry* LegacyShardReader::FindTerm(const string& term) const
{
  size_t left = 0;
  size_t right = terms_.size();
  while (left < right)
  {
    const size_t mid = left + ((right - left) / 2u);
    if (terms_[mid].info.term < term)
      left = mid + 1u;
    else if (term < terms_[mid].info.term)
      right = mid;
    else
      return &terms_[mid];
  }
  return nullptr;
}

void LegacyShardReader::SetError(const char* message)
{
  error_ = string(message ? message : "legacy shard reader error");
}

void LegacyShardReader::SetErrnoError(const char* operation, const string& path)
{
  error_ = string(operation ? operation : "operation");
  error_ += string(": ");
  error_ += path;
  error_ += string(": ");
  error_ += string(std::strerror(errno));
}

} // namespace search
} // namespace avengine
