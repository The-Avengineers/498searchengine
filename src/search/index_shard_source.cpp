#include "avengine/indexer/indexer.h"
#include <avengine/codec.h>
#include <avengine/search/index_shard_source.h>

#include <avengine/avengine_lib/string_slice.h>
#include <avengine/search/search_common.h>

#include <fcntl.h>
#include <cstdio>
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
constexpr uint64_t kShardLoadCacheMagic = 0x4156534649434143ULL; // "AVSFICAC"
constexpr uint64_t kShardLoadCacheVersion = 1;
constexpr uint64_t kRankingInputLayoutVersion = 1;

class CacheReader
{
public:
  CacheReader(const uint8_t* data, size_t size) : cursor_(data), end_(data + size) {}

  bool readU64(uint64_t* out)
  {
    if (!out || static_cast<size_t>(end_ - cursor_) < sizeof(uint64_t))
      return false;

    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
      value |= static_cast<uint64_t>(cursor_[i]) << (i * 8);
    cursor_ += sizeof(uint64_t);
    *out = value;
    return true;
  }

  bool empty() const
  {
    return cursor_ == end_;
  }

private:
  const uint8_t* cursor_ = nullptr;
  const uint8_t* end_ = nullptr;
};

string CachePathForShard(const string& shardPath)
{
  string cachePath(shardPath);
  cachePath += string(".loadcache");
  return cachePath;
}

string TempCachePath(const string& cachePath)
{
  char suffix[64];
  std::snprintf(suffix, sizeof(suffix), ".tmp.%ld", static_cast<long>(getpid()));

  string tmp(cachePath);
  tmp += string(suffix);
  return tmp;
}

uint64_t StatMtimeSec(const struct stat& st)
{
#if defined(__APPLE__) || defined(__MACH__)
  return static_cast<uint64_t>(st.st_mtimespec.tv_sec);
#else
  return static_cast<uint64_t>(st.st_mtim.tv_sec);
#endif
}

uint64_t StatMtimeNsec(const struct stat& st)
{
#if defined(__APPLE__) || defined(__MACH__)
  return static_cast<uint64_t>(st.st_mtimespec.tv_nsec);
#else
  return static_cast<uint64_t>(st.st_mtim.tv_nsec);
#endif
}

bool RangeInFile(uint64_t offset, uint64_t len, uint64_t fileSize)
{
  return offset <= fileSize && len <= fileSize - offset;
}

bool ToU32(uint64_t value, uint32_t* out)
{
  if (!out || value > UINT32_MAX)
    return false;
  *out = static_cast<uint32_t>(value);
  return true;
}

bool ToU16(uint64_t value, uint16_t* out)
{
  if (!out || value > UINT16_MAX)
    return false;
  *out = static_cast<uint16_t>(value);
  return true;
}

bool ToSize(uint64_t value, size_t* out)
{
  if (!out || value > SIZE_MAX)
    return false;
  *out = static_cast<size_t>(value);
  return true;
}

bool WriteAll(int fd, const uint8_t* data, size_t size)
{
  size_t written = 0;
  while (written < size)
  {
    const ssize_t n = write(fd, data + written, size - written);
    if (n <= 0)
      return false;
    written += static_cast<size_t>(n);
  }
  return true;
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
  const uint64_t documentCount = headerReader.readU64();
  const uint64_t uniqueWords = headerReader.readU64();
  const uint64_t tokenOffset = headerReader.readU64();
  uint64_t rankingInputSize = headerReader.readU64(); // ranking-input size

  if (rankingInputSize != sizeof(RankingInput))
  {
    Reset();
    return false;
  }

  if (magic != kIndexMagic)
  {
    Reset();
    return false;
  }

  const SourceFingerprint fingerprint = {
      .fileSize = static_cast<uint64_t>(fileSize_),
      .mtimeSec = StatMtimeSec(st),
      .mtimeNsec = StatMtimeNsec(st),
      .documentCount = documentCount,
      .uniqueWords = uniqueWords,
      .tokenOffset = tokenOffset,
      .rankingInputSize = rankingInputSize,
  };

  if (LoadDerivedCache(fingerprint))
  {
    cacheStatus_ = ShardLoadCacheStatus::Hit;
    return true;
  }

  if (!ParseDictionary(uniqueWords) || !ParseDocumentMetadata())
  {
    Reset();
    return false;
  }

  cacheStatus_ =
      WriteDerivedCache(fingerprint) ? ShardLoadCacheStatus::Rebuilt
                                     : ShardLoadCacheStatus::RebuiltWithoutWrite;
  return true;
}

bool IndexShardSource::IsLoaded() const
{
  return data_ != nullptr;
}

ShardLoadCacheStatus IndexShardSource::GetCacheStatus() const
{
  return cacheStatus_;
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

bool IndexShardSource::LoadDerivedCache(const SourceFingerprint& fingerprint)
{
  const string cachePath = CachePathForShard(path_);
  const int fd = open(cachePath.cstr(), O_RDONLY);
  if (fd < 0)
    return false;

  struct stat st = {};
  if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(13u * sizeof(uint64_t)))
  {
    close(fd);
    return false;
  }

  uint8_t* mapped =
      static_cast<uint8_t*>(mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE,
                                 fd, 0));
  close(fd);
  if (mapped == MAP_FAILED)
    return false;

  CacheReader r(mapped, static_cast<size_t>(st.st_size));
  uint64_t magic = 0;
  uint64_t version = 0;
  uint64_t fileSize = 0;
  uint64_t mtimeSec = 0;
  uint64_t mtimeNsec = 0;
  uint64_t sourceMagic = 0;
  uint64_t documentCount = 0;
  uint64_t uniqueWords = 0;
  uint64_t tokenOffset = 0;
  uint64_t rankingInputSize = 0;
  uint64_t locationSize = 0;
  uint64_t rankingInputLayout = 0;
  uint64_t cachedPostingsStart = 0;
  uint64_t termCount = 0;
  uint64_t cachedDocumentCount = 0;

  bool ok = r.readU64(&magic) && r.readU64(&version) && r.readU64(&fileSize) &&
            r.readU64(&mtimeSec) && r.readU64(&mtimeNsec) && r.readU64(&sourceMagic) &&
            r.readU64(&documentCount) && r.readU64(&uniqueWords) &&
            r.readU64(&tokenOffset) && r.readU64(&rankingInputSize) &&
            r.readU64(&locationSize) && r.readU64(&rankingInputLayout) &&
            r.readU64(&cachedPostingsStart) && r.readU64(&termCount) &&
            r.readU64(&cachedDocumentCount);

  ok = ok && magic == kShardLoadCacheMagic && version == kShardLoadCacheVersion &&
       fileSize == fingerprint.fileSize && mtimeSec == fingerprint.mtimeSec &&
       mtimeNsec == fingerprint.mtimeNsec && sourceMagic == kIndexMagic &&
       documentCount == fingerprint.documentCount && uniqueWords == fingerprint.uniqueWords &&
       tokenOffset == fingerprint.tokenOffset && rankingInputSize == fingerprint.rankingInputSize &&
       locationSize == sizeof(Location) && rankingInputLayout == kRankingInputLayoutVersion &&
       termCount == fingerprint.uniqueWords && cachedDocumentCount == fingerprint.documentCount &&
       cachedPostingsStart <= fingerprint.fileSize && termCount <= SIZE_MAX &&
       cachedDocumentCount <= SIZE_MAX;

  vector<TermIndex> cachedTerms;
  vector<DocumentInfo> cachedDocuments;
  vector<DocSlice> cachedUrls;
  vector<DocSlice> cachedTitles;
  vector<DocSlice> cachedDescriptions;

  if (ok)
    cachedTerms.reserve(static_cast<size_t>(termCount));

  for (uint64_t i = 0; ok && i < termCount; ++i)
  {
    uint64_t termOffset = 0;
    uint64_t termLen = 0;
    uint64_t startOffset = 0;
    uint64_t endOffset = 0;
    uint32_t termOffset32 = 0;
    uint32_t termLen32 = 0;
    size_t startSize = 0;
    size_t endSize = 0;

    ok = r.readU64(&termOffset) && r.readU64(&termLen) && r.readU64(&startOffset) &&
         r.readU64(&endOffset) && ToU32(termOffset, &termOffset32) &&
         ToU32(termLen, &termLen32) && ToSize(startOffset, &startSize) &&
         ToSize(endOffset, &endSize) && RangeInFile(termOffset, termLen, fingerprint.fileSize) &&
         startOffset <= endOffset && endOffset <= fingerprint.fileSize;

    if (ok)
      cachedTerms.pushBack({termOffset32, termLen32, startSize, endSize});
  }

  if (ok)
  {
    cachedDocuments.reserve(static_cast<size_t>(cachedDocumentCount));
    cachedUrls.reserve(static_cast<size_t>(cachedDocumentCount));
    cachedTitles.reserve(static_cast<size_t>(cachedDocumentCount));
    cachedDescriptions.reserve(static_cast<size_t>(cachedDocumentCount));
  }

  for (uint64_t i = 0; ok && i < cachedDocumentCount; ++i)
  {
    uint64_t documentId = 0;
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t titleLength = 0;
    uint64_t urlLength = 0;
    uint64_t domainDepth = 0;
    uint64_t depth = 0;
    uint64_t rankingUrlLength = 0;
    uint64_t titleWordCount = 0;
    uint64_t seedSource = 0;
    uint64_t urlOff = 0;
    uint64_t urlLen = 0;
    uint64_t titleOff = 0;
    uint64_t titleLen = 0;
    uint64_t descOff = 0;
    uint64_t descLen = 0;

    ok = r.readU64(&documentId) && r.readU64(&start) && r.readU64(&end) &&
         r.readU64(&titleLength) && r.readU64(&urlLength) && r.readU64(&domainDepth) &&
         r.readU64(&depth) && r.readU64(&rankingUrlLength) && r.readU64(&titleWordCount) &&
         r.readU64(&seedSource) && r.readU64(&urlOff) && r.readU64(&urlLen) &&
         r.readU64(&titleOff) && r.readU64(&titleLen) && r.readU64(&descOff) &&
         r.readU64(&descLen);

    uint32_t documentId32 = 0;
    uint32_t titleLength32 = 0;
    uint32_t urlLength32 = 0;
    uint32_t domainDepth32 = 0;
    uint32_t depth32 = 0;
    uint16_t rankingUrlLength16 = 0;
    uint16_t titleWordCount16 = 0;
    uint16_t seedSource16 = 0;
    uint32_t urlOff32 = 0;
    uint32_t urlLen32 = 0;
    uint32_t titleOff32 = 0;
    uint32_t titleLen32 = 0;
    uint32_t descOff32 = 0;
    uint32_t descLen32 = 0;
    size_t startSize = 0;
    size_t endSize = 0;

    ok = ok && ToU32(documentId, &documentId32) && ToSize(start, &startSize) &&
         ToSize(end, &endSize) && ToU32(titleLength, &titleLength32) &&
         ToU32(urlLength, &urlLength32) && ToU32(domainDepth, &domainDepth32) &&
         ToU32(depth, &depth32) && ToU16(rankingUrlLength, &rankingUrlLength16) &&
         ToU16(titleWordCount, &titleWordCount16) && ToU16(seedSource, &seedSource16) &&
         ToU32(urlOff, &urlOff32) && ToU32(urlLen, &urlLen32) &&
         ToU32(titleOff, &titleOff32) && ToU32(titleLen, &titleLen32) &&
         ToU32(descOff, &descOff32) && ToU32(descLen, &descLen32) &&
         RangeInFile(urlOff, urlLen, fingerprint.fileSize) &&
         RangeInFile(titleOff, titleLen, fingerprint.fileSize) &&
         RangeInFile(descOff, descLen, fingerprint.fileSize);

    if (ok)
    {
      const RankingInput rankingInput = {.domainDepth = domainDepth32,
                                         .depth = depth32,
                                         .urlLength = rankingUrlLength16,
                                         .titleWordCount = titleWordCount16,
                                         .seedSource = seedSource16};
      cachedDocuments.pushBack(
          {documentId32, startSize, endSize, titleLength32, urlLength32, rankingInput});
      cachedUrls.pushBack({urlOff32, urlLen32});
      cachedTitles.pushBack({titleOff32, titleLen32});
      cachedDescriptions.pushBack({descOff32, descLen32});
    }
  }

  ok = ok && r.empty();
  munmap(mapped, static_cast<size_t>(st.st_size));

  if (!ok)
    return false;

  size_t postingsStart = 0;
  if (!ToSize(cachedPostingsStart, &postingsStart))
    return false;

  postingsStart_ = postingsStart;
  termIndex_.swap(cachedTerms);
  documents_.swap(cachedDocuments);
  docUrls_.swap(cachedUrls);
  docTitles_.swap(cachedTitles);
  docDescriptions_.swap(cachedDescriptions);
  return true;
}

bool IndexShardSource::WriteDerivedCache(const SourceFingerprint& fingerprint) const
{
  if (termIndex_.size() != fingerprint.uniqueWords || documents_.size() != docUrls_.size() ||
      documents_.size() != docTitles_.size() || documents_.size() != docDescriptions_.size())
  {
    return false;
  }

  Writer w;
  w.writeU64(kShardLoadCacheMagic);
  w.writeU64(kShardLoadCacheVersion);
  w.writeU64(fingerprint.fileSize);
  w.writeU64(fingerprint.mtimeSec);
  w.writeU64(fingerprint.mtimeNsec);
  w.writeU64(kIndexMagic);
  w.writeU64(fingerprint.documentCount);
  w.writeU64(fingerprint.uniqueWords);
  w.writeU64(fingerprint.tokenOffset);
  w.writeU64(fingerprint.rankingInputSize);
  w.writeU64(sizeof(Location));
  w.writeU64(kRankingInputLayoutVersion);
  w.writeU64(postingsStart_);
  w.writeU64(termIndex_.size());
  w.writeU64(documents_.size());

  for (size_t i = 0; i < termIndex_.size(); ++i)
  {
    const TermIndex& entry = termIndex_[i];
    w.writeU64(entry.termOffset);
    w.writeU64(entry.termLen);
    w.writeU64(entry.startOffset);
    w.writeU64(entry.endOffset);
  }

  for (size_t i = 0; i < documents_.size(); ++i)
  {
    const DocumentInfo& doc = documents_[i];
    const DocSlice& url = docUrls_[i];
    const DocSlice& title = docTitles_[i];
    const DocSlice& description = docDescriptions_[i];

    w.writeU64(doc.documentId);
    w.writeU64(doc.start);
    w.writeU64(doc.end);
    w.writeU64(doc.titleLength);
    w.writeU64(doc.urlLength);
    w.writeU64(doc.rankingInput.domainDepth);
    w.writeU64(doc.rankingInput.depth);
    w.writeU64(doc.rankingInput.urlLength);
    w.writeU64(doc.rankingInput.titleWordCount);
    w.writeU64(doc.rankingInput.seedSource);
    w.writeU64(url.offset);
    w.writeU64(url.len);
    w.writeU64(title.offset);
    w.writeU64(title.len);
    w.writeU64(description.offset);
    w.writeU64(description.len);
  }

  const string cachePath = CachePathForShard(path_);
  const string tmpPath = TempCachePath(cachePath);
  const int fd = open(tmpPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return false;

  bool ok = WriteAll(fd, w.data(), w.size());
  if (ok)
    (void)fsync(fd);
  if (close(fd) != 0)
    ok = false;

  if (ok && rename(tmpPath.cstr(), cachePath.cstr()) != 0)
    ok = false;

  if (!ok)
    unlink(tmpPath.cstr());
  return ok;
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
  cacheStatus_ = ShardLoadCacheStatus::NotLoaded;
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
