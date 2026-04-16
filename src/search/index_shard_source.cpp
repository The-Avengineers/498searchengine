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
// Version 2: body stores raw binary struct arrays instead of field-by-field uint64.
// Version 1 caches are silently invalidated and rebuilt.
constexpr uint64_t kShardLoadCacheVersion = 2;
constexpr uint64_t kRankingInputLayoutVersion = 1;
// Header is 18 × uint64 = 144 bytes.
constexpr size_t kCacheHeaderBytes = 18 * sizeof(uint64_t);

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
  if (docId >= documentSize_)
    return string();
  const DocSlice& s = docUrlsData_[docId];
  return string_slice(reinterpret_cast<char*>(data_ + s.offset), s.len).toString();
}

string IndexShardSource::GetDocumentTitle(uint32_t docId) const
{
  if (docId >= documentSize_)
    return string();
  const DocSlice& s = docTitlesData_[docId];
  return string_slice(reinterpret_cast<char*>(data_ + s.offset), s.len).toString();
}

string IndexShardSource::GetDocumentDescription(uint32_t docId) const
{
  if (docId >= documentSize_)
    return string();
  const DocSlice& s = docDescriptionsData_[docId];
  return string_slice(reinterpret_cast<char*>(data_ + s.offset), s.len).toString();
}

const DocumentInfo* IndexShardSource::GetDocumentInfo(uint32_t docId) const
{
  if (docId >= documentSize_)
    return nullptr;
  return &documentsData_[docId];
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
  return new ISRWord(positions, documentsData_, documentSize_, documentFrequency);
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
                         header.collectionFrequency, documentsData_, documentSize_,
                         static_cast<uint32_t>(documentFrequency));
}

ISREndDoc* IndexShardSource::OpenEndDocIsr() const
{
  if (documentSize_ == 0)
    return nullptr;
  return new ISREndDoc(documentsData_, documentSize_);
}

bool IndexShardSource::ParseDictionary(uint64_t uniqueWords)
{
  termIndexStorage_.clear();
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

  termIndexStorage_.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i)
  {
    const size_t start = postingsStart_ + static_cast<size_t>(raw[i].postingOffset);
    const size_t end = i + 1 < raw.size()
        ? postingsStart_ + static_cast<size_t>(raw[i + 1].postingOffset)
        : postingsStart_ + postingsBytes;
    if (start > end || end > fileSize_)
      return false;
    termIndexStorage_.pushBack({raw[i].termOffset, raw[i].termLen, start, end});
  }

  // Sort by term string for binary search at query time.
  termIndexStorage_.sort([this](const TermIndex& a, const TermIndex& b) {
    const string_slice sa(reinterpret_cast<char*>(data_ + a.termOffset), a.termLen);
    const string_slice sb(reinterpret_cast<char*>(data_ + b.termOffset), b.termLen);
    return sa < sb;
  });

  termIndexData_ = termIndexStorage_.begin();
  termIndexSize_ = termIndexStorage_.size();
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
    documentsStorage_.pushBack(
        {static_cast<uint32_t>(docId), start, end,
         static_cast<uint32_t>(titleLen), static_cast<uint32_t>(urlLen), rankingInput});
    docUrlsStorage_.pushBack({urlOff, static_cast<uint32_t>(urlLen)});
    docTitlesStorage_.pushBack({titleOff, static_cast<uint32_t>(titleLen)});
    docDescriptionsStorage_.pushBack({descOff, static_cast<uint32_t>(descLen)});
    start = end + 1;
  }

  if (documentsStorage_.empty())
    return false;

  documentsData_ = documentsStorage_.begin();
  docUrlsData_ = docUrlsStorage_.begin();
  docTitlesData_ = docTitlesStorage_.begin();
  docDescriptionsData_ = docDescriptionsStorage_.begin();
  documentSize_ = documentsStorage_.size();
  return true;
}

bool IndexShardSource::LoadDerivedCache(const SourceFingerprint& fingerprint)
{
  // Layout validation — these fire at compile time if struct sizes change.
  static_assert(sizeof(TermIndex) == 24,
                "TermIndex binary layout changed — bump kShardLoadCacheVersion");
  static_assert(sizeof(DocumentInfo) == 48,
                "DocumentInfo binary layout changed — bump kShardLoadCacheVersion");
  static_assert(sizeof(DocSlice) == 8,
                "DocSlice binary layout changed — bump kShardLoadCacheVersion");

  const string cachePath = CachePathForShard(path_);
  const int fd = open(cachePath.cstr(), O_RDONLY);
  if (fd < 0)
    return false;

  struct stat st = {};
  if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(kCacheHeaderBytes))
  {
    close(fd);
    return false;
  }

  const size_t mappedSize = static_cast<size_t>(st.st_size);
  uint8_t* mapped = static_cast<uint8_t*>(
      mmap(nullptr, mappedSize, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (mapped == MAP_FAILED)
    return false;

  // Parse header — 18 uint64 fields.
  CacheReader r(mapped, mappedSize);
  uint64_t magic = 0, version = 0, fileSize = 0, mtimeSec = 0, mtimeNsec = 0;
  uint64_t sourceMagic = 0, documentCount = 0, uniqueWords = 0, tokenOffset = 0;
  uint64_t rankingInputSize = 0, locationSize = 0, rankingInputLayout = 0;
  uint64_t cachedPostingsStart = 0, termCount = 0, cachedDocumentCount = 0;
  uint64_t cachedSizeofTermIndex = 0, cachedSizeofDocumentInfo = 0, cachedSizeofDocSlice = 0;

  bool ok = r.readU64(&magic) && r.readU64(&version) && r.readU64(&fileSize) &&
            r.readU64(&mtimeSec) && r.readU64(&mtimeNsec) && r.readU64(&sourceMagic) &&
            r.readU64(&documentCount) && r.readU64(&uniqueWords) &&
            r.readU64(&tokenOffset) && r.readU64(&rankingInputSize) &&
            r.readU64(&locationSize) && r.readU64(&rankingInputLayout) &&
            r.readU64(&cachedPostingsStart) && r.readU64(&termCount) &&
            r.readU64(&cachedDocumentCount) &&
            r.readU64(&cachedSizeofTermIndex) && r.readU64(&cachedSizeofDocumentInfo) &&
            r.readU64(&cachedSizeofDocSlice);

  ok = ok && magic == kShardLoadCacheMagic && version == kShardLoadCacheVersion &&
       fileSize == fingerprint.fileSize && mtimeSec == fingerprint.mtimeSec &&
       mtimeNsec == fingerprint.mtimeNsec && sourceMagic == kIndexMagic &&
       documentCount == fingerprint.documentCount && uniqueWords == fingerprint.uniqueWords &&
       tokenOffset == fingerprint.tokenOffset && rankingInputSize == fingerprint.rankingInputSize &&
       locationSize == sizeof(Location) && rankingInputLayout == kRankingInputLayoutVersion &&
       termCount == fingerprint.uniqueWords && cachedDocumentCount == fingerprint.documentCount &&
       cachedPostingsStart <= fingerprint.fileSize && termCount <= SIZE_MAX &&
       cachedDocumentCount <= SIZE_MAX &&
       cachedSizeofTermIndex == sizeof(TermIndex) &&
       cachedSizeofDocumentInfo == sizeof(DocumentInfo) &&
       cachedSizeofDocSlice == sizeof(DocSlice);

  if (!ok)
  {
    munmap(mapped, mappedSize);
    return false;
  }

  const size_t nTerms = static_cast<size_t>(termCount);
  const size_t nDocs = static_cast<size_t>(cachedDocumentCount);

  // Compute flat array offsets in the cache file.
  const size_t termArrayOffset = kCacheHeaderBytes;
  const size_t docsArrayOffset = termArrayOffset + nTerms * sizeof(TermIndex);
  const size_t urlsArrayOffset = docsArrayOffset + nDocs * sizeof(DocumentInfo);
  const size_t titlesArrayOffset = urlsArrayOffset + nDocs * sizeof(DocSlice);
  const size_t descsArrayOffset = titlesArrayOffset + nDocs * sizeof(DocSlice);
  const size_t totalRequired = descsArrayOffset + nDocs * sizeof(DocSlice);

  if (mappedSize < totalRequired)
  {
    munmap(mapped, mappedSize);
    return false;
  }

  // Validate all TermIndex entries reference valid regions in the source file.
  const auto* terms = reinterpret_cast<const TermIndex*>(mapped + termArrayOffset);
  for (size_t i = 0; i < nTerms; ++i)
  {
    if (!RangeInFile(terms[i].termOffset, terms[i].termLen, fingerprint.fileSize) ||
        terms[i].startOffset > terms[i].endOffset ||
        terms[i].endOffset > fingerprint.fileSize)
    {
      munmap(mapped, mappedSize);
      return false;
    }
  }

  // Validate all DocSlice entries reference valid regions in the source file.
  const auto* urls = reinterpret_cast<const DocSlice*>(mapped + urlsArrayOffset);
  const auto* titles = reinterpret_cast<const DocSlice*>(mapped + titlesArrayOffset);
  const auto* descs = reinterpret_cast<const DocSlice*>(mapped + descsArrayOffset);
  for (size_t i = 0; i < nDocs; ++i)
  {
    if (!RangeInFile(urls[i].offset, urls[i].len, fingerprint.fileSize) ||
        !RangeInFile(titles[i].offset, titles[i].len, fingerprint.fileSize) ||
        !RangeInFile(descs[i].offset, descs[i].len, fingerprint.fileSize))
    {
      munmap(mapped, mappedSize);
      return false;
    }
  }

  size_t postingsStart = 0;
  if (!ToSize(cachedPostingsStart, &postingsStart))
  {
    munmap(mapped, mappedSize);
    return false;
  }

  // Cache pages are accessed randomly at query time (only the terms and docs
  // actually touched by a query are ever faulted in).
  madvise(mapped, mappedSize, MADV_RANDOM);

  // Keep the mmap alive — all data pointers point directly into it.
  cacheMapData_ = mapped;
  cacheMapSize_ = mappedSize;

  postingsStart_ = postingsStart;
  termIndexData_ = terms;
  termIndexSize_ = nTerms;
  documentsData_ = reinterpret_cast<const DocumentInfo*>(mapped + docsArrayOffset);
  docUrlsData_ = urls;
  docTitlesData_ = titles;
  docDescriptionsData_ = descs;
  documentSize_ = nDocs;
  return true;
}

bool IndexShardSource::WriteDerivedCache(const SourceFingerprint& fingerprint) const
{
  if (termIndexSize_ != fingerprint.uniqueWords || documentSize_ != fingerprint.documentCount)
    return false;

  Writer w;
  // 18-field header
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
  w.writeU64(static_cast<uint64_t>(postingsStart_));
  w.writeU64(static_cast<uint64_t>(termIndexSize_));
  w.writeU64(static_cast<uint64_t>(documentSize_));
  w.writeU64(sizeof(TermIndex));
  w.writeU64(sizeof(DocumentInfo));
  w.writeU64(sizeof(DocSlice));

  // Raw binary arrays — no per-field serialization.
  w.writeBytes(termIndexData_, termIndexSize_ * sizeof(TermIndex));
  w.writeBytes(documentsData_, documentSize_ * sizeof(DocumentInfo));
  w.writeBytes(docUrlsData_, documentSize_ * sizeof(DocSlice));
  w.writeBytes(docTitlesData_, documentSize_ * sizeof(DocSlice));
  w.writeBytes(docDescriptionsData_, documentSize_ * sizeof(DocSlice));

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
  size_t lo = 0, hi = termIndexSize_;
  while (lo < hi)
  {
    const size_t mid = lo + (hi - lo) / 2;
    const TermIndex& t = termIndexData_[mid];
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
  // Release the persistent .loadcache mmap (cache hit path).
  if (cacheMapData_)
  {
    munmap(cacheMapData_, cacheMapSize_);
    cacheMapData_ = nullptr;
    cacheMapSize_ = 0;
  }

  // Clear heap storage (cache miss path).
  termIndexStorage_.clear();
  documentsStorage_.clear();
  docUrlsStorage_.clear();
  docTitlesStorage_.clear();
  docDescriptionsStorage_.clear();

  // Null out the active view pointers.
  termIndexData_ = nullptr;
  termIndexSize_ = 0;
  documentsData_ = nullptr;
  docUrlsData_ = nullptr;
  docTitlesData_ = nullptr;
  docDescriptionsData_ = nullptr;
  documentSize_ = 0;

  cacheStatus_ = ShardLoadCacheStatus::NotLoaded;
  postingsStart_ = 0;

  const size_t mappedSize = fileSize_;
  fileSize_ = 0;
  if (data_)
  {
    munmap(data_, mappedSize);
    data_ = nullptr;
  }
}

} // namespace search
} // namespace avengine
