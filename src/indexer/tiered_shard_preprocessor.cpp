#include <avengine/avengine_lib/atomic.h>
#include <avengine/avengine_lib/mutex.h>
#include <avengine/avengine_lib/thread.h>
#include <avengine/indexer/indexer.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{

constexpr uint64_t kIndexMagic = 0x008FB8EF84979FF0ULL;
constexpr size_t kTierCount = 3;
constexpr size_t kDefaultMaxThreads = 2;
constexpr size_t kMaxRankThreads = 2;
constexpr size_t kMaxRewriteThreads = 1;
constexpr size_t kRewriteDocsPerChunk = 1024;
constexpr size_t kMaxMetadataStringBytes = 16u * 1024u * 1024u;
constexpr const char* kCheckpointHeader = "AVENGINE_TIERED_PREPROCESS_CHECKPOINT 1";

struct Options
{
  avengine::string inputDir = avengine::string(".");
  avengine::string outputDir = avengine::string("tiered-index");
  avengine::string checkpointPath;
  size_t threads = 0;
  bool force = false;
  bool resetCheckpoint = false;
  bool help = false;
};

struct TermEntry
{
  avengine::string term;
  uint64_t positionsCount = 0;
  uint64_t documentFrequency = 0;
  uint64_t postingOffset = 0;
  size_t postingStart = 0;
  size_t postingEnd = 0;
};

struct DocMeta
{
  avengine::string url;
  avengine::string title;
  avengine::string description;
  RankingInput input = {};
};

struct MappedShard
{
  const uint8_t* data = nullptr;
  size_t size = 0;
  int fd = -1;

  MappedShard() = default;
  MappedShard(const MappedShard&) = delete;
  MappedShard& operator=(const MappedShard&) = delete;

  ~MappedShard()
  {
    reset();
  }

  void reset()
  {
    if (data)
      munmap(const_cast<uint8_t*>(data), size);
    if (fd >= 0)
      close(fd);
    data = nullptr;
    size = 0;
    fd = -1;
  }

  bool load(const avengine::string& path, std::string& reason)
  {
    reset();
    fd = open(path.cstr(), O_RDONLY);
    if (fd < 0)
    {
      reason = "open failed";
      return false;
    }

    struct stat st = {};
    if (fstat(fd, &st) != 0 || st.st_size < 0)
    {
      reason = "stat failed";
      return false;
    }
    if (st.st_size == 0)
    {
      reason = "empty file";
      return false;
    }

    size = static_cast<size_t>(st.st_size);
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED)
    {
      reason = "mmap failed";
      data = nullptr;
      return false;
    }
    data = static_cast<const uint8_t*>(mapped);
    return true;
  }
};

struct ParsedShard
{
  avengine::string path;
  bool isAnchor = false;
  uint64_t documentCount = 0;
  size_t rankingInputSize = 0;
  MappedShard mapping;
  avengine::vector<TermEntry> terms;
  avengine::vector<uint32_t> docEndPositions;
  avengine::vector<DocMeta> docs;

  void clear()
  {
    mapping.reset();
    path = avengine::string();
    isAnchor = false;
    documentCount = 0;
    rankingInputSize = 0;
    terms.clear();
    docEndPositions.clear();
    docs.clear();
  }
};

struct ShardFile
{
  avengine::string path;
  std::string pathStd;
  std::string stem;
  uint64_t size = 0;
  int64_t mtime = 0;
  bool isAnchor = false;
};

struct RankedDoc
{
  avengine::string url;
  double score = 0.0;
};

struct RankBoundaries
{
  size_t totalDocs = 0;
  RankedDoc tier0Last;
  RankedDoc tier1Last;
};

struct TokenAtPosition
{
  uint32_t position = 0;
  uint32_t termIndex = 0;
};

struct ProgressState
{
  const char* phase = "";
  size_t totalShards = 0;
  avengine::atomic_counter processedShards;
  avengine::atomic_counter documents;
  avengine::atomic_counter failedShards;
  avengine::atomic_counter done;
};

struct CheckpointEntry
{
  std::string path;
  uint64_t size = 0;
  int64_t mtime = 0;
  bool isAnchor = false;
  size_t documents = 0;
  bool completed = false;
  bool failed = false;
  std::string reason;
  std::vector<std::string> outputs;
};

struct CheckpointState
{
  std::vector<CheckpointEntry> entries;
};

void PrintProgress(const ProgressState& state, bool final)
{
  printf("[tiered_shard_preprocessor] %s progress: %d/%zu shard(s), docs=%d, failed=%d%s\n",
         state.phase, state.processedShards.get_val(), state.totalShards,
         state.documents.get_val(), state.failedShards.get_val(), final ? " done" : "");
  fflush(stdout);
}

void* ProgressThread(void* raw)
{
  ProgressState* state = static_cast<ProgressState*>(raw);
  int lastProcessed = -1;
  int lastDocs = -1;
  int lastFailed = -1;

  while (!state->done.load())
  {
    usleep(250000);
    const int processed = state->processedShards.get_val();
    const int docs = state->documents.get_val();
    const int failed = state->failedShards.get_val();
    if (processed != lastProcessed || docs != lastDocs || failed != lastFailed)
    {
      PrintProgress(*state, false);
      lastProcessed = processed;
      lastDocs = docs;
      lastFailed = failed;
    }
  }

  PrintProgress(*state, true);
  return nullptr;
}

std::string ToStdString(const avengine::string& value)
{
  return std::string(value.cstr(), value.size());
}

avengine::string ToAvString(const std::string& value)
{
  avengine::string out;
  for (char c : value)
    out.pushBack(c);
  return out;
}

bool ParseSize(const char* text, size_t& out)
{
  if (!text || text[0] == '\0')
    return false;
  size_t value = 0;
  for (size_t i = 0; text[i] != '\0'; ++i)
  {
    if (text[i] < '0' || text[i] > '9')
      return false;
    const size_t digit = static_cast<size_t>(text[i] - '0');
    if (value > (static_cast<size_t>(-1) - digit) / 10u)
      return false;
    value = (value * 10u) + digit;
  }
  out = value;
  return true;
}

bool ParseU64(const std::string& text, uint64_t& out)
{
  if (text.empty())
    return false;
  uint64_t value = 0;
  for (char c : text)
  {
    if (c < '0' || c > '9')
      return false;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (value > (~uint64_t{0} - digit) / 10u)
      return false;
    value = (value * 10u) + digit;
  }
  out = value;
  return true;
}

size_t DefaultThreadCount()
{
  const long configured = sysconf(_SC_NPROCESSORS_ONLN);
  if (configured <= 0)
    return 1;
  const size_t cpuCount = static_cast<size_t>(configured);
  return cpuCount < kDefaultMaxThreads ? cpuCount : kDefaultMaxThreads;
}

size_t ClampThreadCount(size_t requested, size_t workItems)
{
  if (workItems == 0)
    return 1;
  size_t count = requested == 0 ? DefaultThreadCount() : requested;
  if (count == 0)
    count = 1;
  if (count > workItems)
    count = workItems;
  return count;
}

size_t ClampPhaseThreadCount(size_t requested, size_t workItems, size_t phaseMax)
{
  size_t count = ClampThreadCount(requested, workItems);
  if (count > phaseMax)
    count = phaseMax;
  return count;
}

bool CheckedAdd(size_t lhs, size_t rhs, size_t& out)
{
  if (lhs > static_cast<size_t>(-1) - rhs)
    return false;
  out = lhs + rhs;
  return true;
}

bool HasBytes(size_t offset, size_t length, size_t total)
{
  return offset <= total && length <= total - offset;
}

uint64_t ReadU64Le(const uint8_t* p)
{
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

bool ReadU64At(const uint8_t* data, size_t size, size_t offset, uint64_t& out)
{
  if (!HasBytes(offset, sizeof(uint64_t), size))
    return false;
  out = ReadU64Le(data + offset);
  return true;
}

void CopyBytes(void* destination, const void* source, size_t size)
{
  uint8_t* dst = static_cast<uint8_t*>(destination);
  const uint8_t* src = static_cast<const uint8_t*>(source);
  for (size_t i = 0; i < size; ++i)
    dst[i] = src[i];
}

bool ReadLpStringAt(const uint8_t* data, size_t size, size_t& cursor, avengine::string& out)
{
  uint64_t len = 0;
  if (!ReadU64At(data, size, cursor, len))
    return false;
  cursor += sizeof(uint64_t);
  if (len > kMaxMetadataStringBytes || len > static_cast<uint64_t>(size - cursor))
    return false;

  out = avengine::string();
  for (size_t i = 0; i < static_cast<size_t>(len); ++i)
    out.pushBack(static_cast<char>(data[cursor + i]));
  cursor += static_cast<size_t>(len);
  return true;
}

bool ReadVarU32(const uint8_t*& cursor, const uint8_t* end, uint32_t& out)
{
  uint32_t result = 0;
  uint32_t shift = 0;
  while (cursor < end && shift < 35)
  {
    const uint8_t byte = *cursor++;
    if (shift == 28 && (byte & 0xF0u) != 0)
      return false;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0)
    {
      out = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

bool SkipPostingHeaderAndSkips(const uint8_t* begin, const uint8_t* end, uint64_t& docFreq,
                               uint64_t& collFreq, uint64_t& skipCount, const uint8_t*& cursor)
{
  if (!begin || !end || begin > end || static_cast<size_t>(end - begin) < 24u)
    return false;
  docFreq = ReadU64Le(begin);
  collFreq = ReadU64Le(begin + 8);
  skipCount = ReadU64Le(begin + 16);
  cursor = begin + 24;

  const size_t remaining = static_cast<size_t>(end - cursor);
  if (skipCount > remaining / 3u)
    return false;

  uint32_t ignored = 0;
  for (uint64_t s = 0; s < skipCount; ++s)
  {
    for (int field = 0; field < 3; ++field)
    {
      if (!ReadVarU32(cursor, end, ignored))
        return false;
    }
  }
  return true;
}

bool DecodePositions(const ParsedShard& shard, const TermEntry& term, avengine::vector<uint32_t>& out)
{
  out.clear();
  if (!HasBytes(term.postingStart, 24u, shard.mapping.size) ||
      term.postingEnd > shard.mapping.size || term.postingStart > term.postingEnd)
    return false;

  const uint8_t* begin = shard.mapping.data + term.postingStart;
  const uint8_t* end = shard.mapping.data + term.postingEnd;
  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
  const uint8_t* cursor = nullptr;
  if (!SkipPostingHeaderAndSkips(begin, end, documentFrequency, collectionFrequency, skipCount,
                                 cursor))
  {
    return false;
  }
  if (documentFrequency != term.documentFrequency || collectionFrequency != term.positionsCount)
    return false;
  if (collectionFrequency > static_cast<uint64_t>(end - cursor))
    return false;

  out.reserve(static_cast<size_t>(collectionFrequency));
  uint32_t absolute = 0;
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    uint32_t delta = 0;
    if (!ReadVarU32(cursor, end, delta) || delta > UINT32_MAX - absolute)
      return false;
    absolute += delta;
    out.pushBack(absolute);
  }
  return true;
}

bool HasSfiSuffix(const avengine::string& name)
{
  const size_t n = name.size();
  return n >= 4 && name[n - 4] == '.' && name[n - 3] == 's' && name[n - 2] == 'f' &&
         name[n - 1] == 'i';
}

bool EndsWithLiteral(const avengine::string& name, const char* suffix)
{
  size_t suffixLen = 0;
  while (suffix[suffixLen] != '\0')
    ++suffixLen;
  if (name.size() < suffixLen)
    return false;
  const size_t start = name.size() - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i)
  {
    if (name[start + i] != suffix[i])
      return false;
  }
  return true;
}

avengine::string Basename(const avengine::string& path)
{
  size_t start = 0;
  for (size_t i = 0; i < path.size(); ++i)
  {
    if (path[i] == '/')
      start = i + 1;
  }

  avengine::string out;
  for (size_t i = start; i < path.size(); ++i)
    out.pushBack(path[i]);
  return out;
}

bool IsAnchorShardName(const avengine::string& path)
{
  const avengine::string name = Basename(path);
  const char anchorPrefix[] = "anchor_";
  bool startsWithAnchor = name.size() >= 7;
  for (size_t i = 0; i < 7 && startsWithAnchor; ++i)
    startsWithAnchor = name[i] == anchorPrefix[i];
  if (startsWithAnchor)
    return true;
  return name.find("_anchor_") != avengine::string::npos;
}

avengine::string JoinPath(const avengine::string& dir, const avengine::string& name)
{
  avengine::string path(dir);
  if (dir.size() == 0 || dir[dir.size() - 1] == '/')
  {
    path += name;
    return path;
  }
  path.pushBack('/');
  path += name;
  return path;
}

std::string SanitizedStem(const avengine::string& path)
{
  const avengine::string base = Basename(path);
  const size_t suffixStart = HasSfiSuffix(base) ? base.size() - 4u : base.size();
  std::string out;
  out.reserve(suffixStart);
  for (size_t i = 0; i < suffixStart; ++i)
  {
    const char c = base[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    out.push_back(ok ? c : '_');
  }
  if (out.empty())
    out = "shard";
  uint64_t hash = 1469598103934665603ULL;
  const std::string fullPath = ToStdString(path);
  for (unsigned char c : fullPath)
  {
    hash ^= static_cast<uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  char suffix[32];
  snprintf(suffix, sizeof(suffix), "_%016llx", static_cast<unsigned long long>(hash));
  out += suffix;
  return out;
}

bool EnsureOutputDirectory(const avengine::string& path)
{
  struct stat st = {};
  if (stat(path.cstr(), &st) == 0)
    return (st.st_mode & S_IFDIR) != 0;
  return mkdir(path.cstr(), 0755) == 0;
}

bool FileExists(const std::string& path)
{
  struct stat st = {};
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void RemoveTempFiles(const avengine::string& path)
{
  DIR* dir = opendir(path.cstr());
  if (!dir)
    return;
  while (dirent* ent = readdir(dir))
  {
    const avengine::string name(ent->d_name);
    if (EndsWithLiteral(name, ".sfi.tmp"))
      unlink(JoinPath(path, name).cstr());
  }
  closedir(dir);
}

bool PrepareOutputDirectory(const avengine::string& path, const avengine::string& checkpointPath,
                            bool force, bool resetCheckpoint)
{
  if (!EnsureOutputDirectory(path))
  {
    fprintf(stderr, "failed to create output directory: %s\n", path.cstr());
    return false;
  }

  RemoveTempFiles(path);
  if (resetCheckpoint)
    unlink(checkpointPath.cstr());

  DIR* dir = opendir(path.cstr());
  if (!dir)
    return false;

  avengine::vector<avengine::string> sfiFiles;
  while (dirent* ent = readdir(dir))
  {
    const avengine::string name(ent->d_name);
    if (HasSfiSuffix(name))
      sfiFiles.pushBack(JoinPath(path, name));
  }
  closedir(dir);

  const bool hasCheckpoint = FileExists(ToStdString(checkpointPath));
  if (!sfiFiles.empty() && !force && !hasCheckpoint)
  {
    fprintf(stderr, "output directory already contains .sfi files; pass --force to replace\n");
    return false;
  }

  if (!sfiFiles.empty() && (resetCheckpoint || (!hasCheckpoint && force)))
  {
    for (const avengine::string& file : sfiFiles)
      unlink(file.cstr());
  }
  return true;
}

std::vector<ShardFile> DiscoverSfiFiles(const avengine::string& dirPath)
{
  std::vector<ShardFile> files;
  DIR* dir = opendir(dirPath.cstr());
  if (!dir)
    return files;

  while (dirent* ent = readdir(dir))
  {
    if (ent->d_name[0] == '.')
      continue;
    const avengine::string name(ent->d_name);
    if (!HasSfiSuffix(name))
      continue;

    ShardFile file;
    file.path = JoinPath(dirPath, name);
    file.pathStd = ToStdString(file.path);
    file.stem = SanitizedStem(file.path);
    file.isAnchor = IsAnchorShardName(file.path);
    struct stat st = {};
    if (stat(file.path.cstr(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0)
    {
      file.size = static_cast<uint64_t>(st.st_size);
      file.mtime = static_cast<int64_t>(st.st_mtime);
    }
    files.push_back(file);
  }
  closedir(dir);

  std::sort(files.begin(), files.end(), [](const ShardFile& lhs, const ShardFile& rhs) {
    return lhs.pathStd < rhs.pathStd;
  });
  return files;
}

size_t CountPrimaryFiles(const std::vector<ShardFile>& files)
{
  size_t count = 0;
  for (const ShardFile& file : files)
  {
    if (!file.isAnchor)
      ++count;
  }
  return count;
}

bool ReadHeader(const ParsedShard& shard, uint64_t& magic, uint64_t& documentCount,
                uint64_t& uniqueWords, uint64_t& tokenOffset, uint64_t& rankingInputSize)
{
  const uint8_t* data = shard.mapping.data;
  const size_t size = shard.mapping.size;
  return ReadU64At(data, size, 0, magic) && ReadU64At(data, size, 8, documentCount) &&
         ReadU64At(data, size, 16, uniqueWords) && ReadU64At(data, size, 24, tokenOffset) &&
         ReadU64At(data, size, 32, rankingInputSize);
}

bool ParseDocEnd(ParsedShard& shard, std::string& reason)
{
  const TermEntry* docEnd = nullptr;
  for (const TermEntry& term : shard.terms)
  {
    if (term.term == DOCEND_TOKEN)
    {
      docEnd = &term;
      break;
    }
  }
  if (!docEnd)
  {
    reason = "missing DOCUMENT_END";
    return false;
  }

  if (!DecodePositions(shard, *docEnd, shard.docEndPositions))
  {
    reason = "invalid DOCUMENT_END positions";
    return false;
  }

  const uint8_t* begin = shard.mapping.data + docEnd->postingStart;
  const uint8_t* end = shard.mapping.data + docEnd->postingEnd;
  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
  const uint8_t* cursorPtr = nullptr;
  if (!SkipPostingHeaderAndSkips(begin, end, documentFrequency, collectionFrequency, skipCount,
                                 cursorPtr))
  {
    reason = "invalid DOCUMENT_END header";
    return false;
  }
  if (documentFrequency != collectionFrequency || documentFrequency != shard.documentCount ||
      documentFrequency != shard.docEndPositions.size())
  {
    reason = "DOCUMENT_END count mismatch";
    return false;
  }
  if (documentFrequency > (shard.mapping.size / (24u + shard.rankingInputSize)))
  {
    reason = "impossible document count";
    return false;
  }

  uint32_t ignored = 0;
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    if (!ReadVarU32(cursorPtr, end, ignored))
    {
      reason = "truncated DOCUMENT_END deltas";
      return false;
    }
  }

  shard.docs.clear();
  shard.docs.reserve(static_cast<size_t>(documentFrequency));
  size_t cursor = static_cast<size_t>(cursorPtr - shard.mapping.data);
  const size_t endOffset = static_cast<size_t>(end - shard.mapping.data);
  for (uint64_t i = 0; i < documentFrequency; ++i)
  {
    DocMeta doc;
    if (!ReadLpStringAt(shard.mapping.data, endOffset, cursor, doc.url) ||
        !ReadLpStringAt(shard.mapping.data, endOffset, cursor, doc.title) ||
        !ReadLpStringAt(shard.mapping.data, endOffset, cursor, doc.description))
    {
      reason = "truncated document metadata";
      return false;
    }
    if (!HasBytes(cursor, shard.rankingInputSize, endOffset))
    {
      reason = "truncated ranking input";
      return false;
    }
    doc.input = {};
    CopyBytes(&doc.input, shard.mapping.data + cursor, shard.rankingInputSize);
    cursor += shard.rankingInputSize;
    shard.docs.pushBack(doc);
  }

  return true;
}

bool ParseShard(const ShardFile& file, ParsedShard& shard, std::string& reason)
{
  shard.clear();
  shard.path = file.path;
  shard.isAnchor = file.isAnchor;
  if (!shard.mapping.load(file.path, reason) || shard.mapping.size < 5u * sizeof(uint64_t))
  {
    if (reason.empty())
      reason = "short shard header";
    return false;
  }

  uint64_t magic = 0;
  uint64_t documentCount = 0;
  uint64_t uniqueWords = 0;
  uint64_t tokenOffset = 0;
  uint64_t rankingInputSize = 0;
  if (!ReadHeader(shard, magic, documentCount, uniqueWords, tokenOffset, rankingInputSize) ||
      magic != kIndexMagic)
  {
    reason = "invalid shard header";
    return false;
  }
  if (rankingInputSize > sizeof(RankingInput))
  {
    reason = "unsupported ranking input size";
    return false;
  }
  if (uniqueWords > (shard.mapping.size - 5u * sizeof(uint64_t)) / 32u + 1u)
  {
    reason = "impossible dictionary size";
    return false;
  }

  shard.documentCount = documentCount;
  shard.rankingInputSize = static_cast<size_t>(rankingInputSize);
  if (documentCount == 0)
    return true;

  size_t cursor = 5u * sizeof(uint64_t);
  shard.terms.reserve(static_cast<size_t>(uniqueWords));
  for (uint64_t i = 0; i < uniqueWords; ++i)
  {
    TermEntry term;
    if (!ReadLpStringAt(shard.mapping.data, shard.mapping.size, cursor, term.term) ||
        !HasBytes(cursor, 24u, shard.mapping.size))
    {
      reason = "truncated dictionary";
      return false;
    }
    term.positionsCount = ReadU64Le(shard.mapping.data + cursor);
    term.documentFrequency = ReadU64Le(shard.mapping.data + cursor + 8);
    term.postingOffset = ReadU64Le(shard.mapping.data + cursor + 16);
    cursor += 24u;
    shard.terms.pushBack(term);
  }

  const size_t postingsStart = cursor;
  std::vector<size_t> order(shard.terms.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
    return shard.terms[lhs].postingOffset < shard.terms[rhs].postingOffset;
  });

  for (size_t oi = 0; oi < order.size(); ++oi)
  {
    TermEntry& term = shard.terms[order[oi]];
    if (term.postingOffset > static_cast<uint64_t>(shard.mapping.size - postingsStart))
    {
      reason = "posting offset outside shard";
      return false;
    }
    size_t start = 0;
    if (!CheckedAdd(postingsStart, static_cast<size_t>(term.postingOffset), start))
    {
      reason = "posting offset overflow";
      return false;
    }
    size_t next = shard.mapping.size;
    if (oi + 1 < order.size())
    {
      const uint64_t nextOffset = shard.terms[order[oi + 1]].postingOffset;
      if (nextOffset > static_cast<uint64_t>(shard.mapping.size - postingsStart) ||
          !CheckedAdd(postingsStart, static_cast<size_t>(nextOffset), next))
      {
        reason = "next posting offset outside shard";
        return false;
      }
    }
    if (start > next || next > shard.mapping.size || !HasBytes(start, 24u, shard.mapping.size))
    {
      reason = "invalid posting bounds";
      return false;
    }
    term.postingStart = start;
    term.postingEnd = next;
  }

  if (!ParseDocEnd(shard, reason))
    return false;
  return true;
}

double PiecewiseDescending(double value, double best, double worst)
{
  if (value <= best)
    return 1.0;
  if (value >= worst)
    return 0.0;
  return (worst - value) / (worst - best);
}

size_t FindCharFrom(const avengine::string& value, char needle, size_t start)
{
  for (size_t i = start; i < value.size(); ++i)
  {
    if (value[i] == needle)
      return i;
  }
  return avengine::string::npos;
}

avengine::string HostFromUrl(const avengine::string& url)
{
  size_t start = 0;
  const size_t scheme = url.find("://");
  if (scheme != avengine::string::npos)
    start = scheme + 3;
  size_t end = start;
  while (end < url.size() && url[end] != '/' && url[end] != '?' && url[end] != '#')
    ++end;

  avengine::string host;
  for (size_t i = start; i < end; ++i)
    host.pushBack(url[i]);
  return host;
}

double DomainScore(const avengine::string& url)
{
  const avengine::string host = HostFromUrl(url);
  if (host.find(".edu") != avengine::string::npos || host.find(".gov") != avengine::string::npos ||
      host.find(".org") != avengine::string::npos)
    return 1.0;
  if (host.find(".com") != avengine::string::npos)
    return 0.6;
  if (host.find(".xyz") != avengine::string::npos || host.find(".net") != avengine::string::npos ||
      host.find(".io") != avengine::string::npos)
    return 0.3;
  return 0.15;
}

double UrlPathDepthScore(const avengine::string& url)
{
  const size_t scheme = url.find("://");
  const size_t pathStart =
      scheme == avengine::string::npos ? FindCharFrom(url, '/', 0) : FindCharFrom(url, '/', scheme + 3);
  if (pathStart == avengine::string::npos)
    return 1.0;

  size_t segments = 0;
  size_t i = pathStart;
  while (i < url.size() && url[i] != '?' && url[i] != '#')
  {
    if (url[i] == '/')
    {
      ++i;
      continue;
    }
    ++segments;
    while (i < url.size() && url[i] != '/' && url[i] != '?' && url[i] != '#')
      ++i;
  }

  static constexpr size_t maxDepth = 6;
  if (segments == 0)
    return 1.0;
  if (segments >= maxDepth)
    return 0.0;
  return static_cast<double>(maxDepth - segments) / static_cast<double>(maxDepth);
}

double StaticRankScore(const DocMeta& doc)
{
  const double seedDomainDepth = PiecewiseDescending(doc.input.domainDepth, 1.0, 5.0);
  const double crawlDepth = PiecewiseDescending(doc.input.depth, 2.0, 8.0);
  const double shortTitle = PiecewiseDescending(doc.input.titleWordCount, 5.0, 15.0);
  const double shortUrl = PiecewiseDescending(doc.url.size(), 10.0, 120.0);
  const double queryPenalty =
      (doc.url.find("?q=") != avengine::string::npos || doc.url.find("&q=") != avengine::string::npos ||
       doc.url.find("&amp;q=") != avengine::string::npos)
          ? 1.0
          : 0.0;

  return (2.5 * seedDomainDepth) + (1.0 * UrlPathDepthScore(doc.url)) + (0.9 * crawlDepth) +
         (0.9 * shortTitle) + (0.9 * shortUrl) + (0.2 * DomainScore(doc.url)) -
         (10.0 * queryPenalty);
}

int TierForRankIndex(size_t index, size_t total)
{
  if (total == 0)
    return 2;
  if (index * 100u < total * 20u)
    return 0;
  if (index * 100u < total * 50u)
    return 1;
  return 2;
}

bool BetterOrEqualRank(const RankedDoc& doc, const RankedDoc& cutoff)
{
  if (doc.score != cutoff.score)
    return doc.score > cutoff.score;
  return doc.url <= cutoff.url;
}

int TierForDoc(const DocMeta& doc, const RankBoundaries& boundaries)
{
  RankedDoc ranked{doc.url, StaticRankScore(doc)};
  if (BetterOrEqualRank(ranked, boundaries.tier0Last))
    return 0;
  if (BetterOrEqualRank(ranked, boundaries.tier1Last))
    return 1;
  return 2;
}

std::string HexEncode(const std::string& text)
{
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size() * 2u);
  for (unsigned char c : text)
  {
    out.push_back(hex[c >> 4]);
    out.push_back(hex[c & 0x0Fu]);
  }
  return out;
}

int HexValue(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

bool HexDecode(const std::string& encoded, std::string& out)
{
  if ((encoded.size() % 2u) != 0)
    return false;
  out.clear();
  out.reserve(encoded.size() / 2u);
  for (size_t i = 0; i < encoded.size(); i += 2)
  {
    const int hi = HexValue(encoded[i]);
    const int lo = HexValue(encoded[i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out.push_back(static_cast<char>((hi << 4) | lo));
  }
  return true;
}

std::vector<std::string> SplitWords(const std::string& line)
{
  std::vector<std::string> out;
  size_t i = 0;
  while (i < line.size())
  {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\n' ||
                               line[i] == '\r'))
    {
      ++i;
    }
    const size_t start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\n' &&
           line[i] != '\r')
    {
      ++i;
    }
    if (i > start)
      out.push_back(line.substr(start, i - start));
  }
  return out;
}

CheckpointEntry* FindCheckpointEntry(CheckpointState& state, const std::string& path)
{
  for (CheckpointEntry& entry : state.entries)
  {
    if (entry.path == path)
      return &entry;
  }
  return nullptr;
}

const CheckpointEntry* FindCheckpointEntry(const CheckpointState& state, const std::string& path)
{
  for (const CheckpointEntry& entry : state.entries)
  {
    if (entry.path == path)
      return &entry;
  }
  return nullptr;
}

bool SameFingerprint(const CheckpointEntry& entry, const ShardFile& file)
{
  return entry.path == file.pathStd && entry.size == file.size && entry.mtime == file.mtime &&
         entry.isAnchor == file.isAnchor;
}

bool LoadCheckpoint(const avengine::string& path, CheckpointState& state)
{
  state.entries.clear();
  FILE* fp = fopen(path.cstr(), "r");
  if (!fp)
    return false;

  char line[65536];
  if (!fgets(line, sizeof(line), fp))
  {
    fclose(fp);
    return false;
  }
  std::string header(line);
  while (!header.empty() && (header.back() == '\n' || header.back() == '\r'))
    header.pop_back();
  if (header != kCheckpointHeader)
  {
    fclose(fp);
    return false;
  }

  while (fgets(line, sizeof(line), fp))
  {
    std::vector<std::string> parts = SplitWords(line);
    if (parts.empty())
      continue;
    if ((parts[0] == "DONE" || parts[0] == "FAIL") && parts.size() >= 6)
    {
      std::string decodedPath;
      if (!HexDecode(parts[1], decodedPath))
        continue;
      uint64_t size = 0;
      uint64_t mtime = 0;
      uint64_t isAnchor = 0;
      uint64_t docs = 0;
      if (!ParseU64(parts[2], size) || !ParseU64(parts[3], mtime) ||
          !ParseU64(parts[4], isAnchor) || !ParseU64(parts[5], docs))
      {
        continue;
      }

      CheckpointEntry* entry = FindCheckpointEntry(state, decodedPath);
      if (!entry)
      {
        CheckpointEntry created;
        created.path = decodedPath;
        state.entries.push_back(created);
        entry = &state.entries.back();
      }
      entry->size = size;
      entry->mtime = static_cast<int64_t>(mtime);
      entry->isAnchor = isAnchor != 0;
      entry->documents = static_cast<size_t>(docs);
      entry->completed = parts[0] == "DONE";
      entry->failed = parts[0] == "FAIL";
      if (entry->failed && parts.size() >= 7)
        HexDecode(parts[6], entry->reason);
    }
    else if (parts[0] == "OUT" && parts.size() >= 3)
    {
      std::string decodedPath;
      std::string decodedOut;
      if (!HexDecode(parts[1], decodedPath) || !HexDecode(parts[2], decodedOut))
        continue;
      CheckpointEntry* entry = FindCheckpointEntry(state, decodedPath);
      if (entry)
        entry->outputs.push_back(decodedOut);
    }
  }

  fclose(fp);
  return true;
}

bool SaveCheckpoint(const avengine::string& path, const CheckpointState& state)
{
  const std::string finalPath = ToStdString(path);
  const std::string tmpPath = finalPath + ".tmp";
  FILE* fp = fopen(tmpPath.c_str(), "w");
  if (!fp)
    return false;

  fprintf(fp, "%s\n", kCheckpointHeader);
  for (const CheckpointEntry& entry : state.entries)
  {
    if (entry.completed)
    {
      fprintf(fp, "DONE %s %llu %lld %d %zu\n", HexEncode(entry.path).c_str(),
              static_cast<unsigned long long>(entry.size), static_cast<long long>(entry.mtime),
              entry.isAnchor ? 1 : 0, entry.documents);
      for (const std::string& output : entry.outputs)
      {
        fprintf(fp, "OUT %s %s\n", HexEncode(entry.path).c_str(), HexEncode(output).c_str());
      }
    }
    else if (entry.failed)
    {
      fprintf(fp, "FAIL %s %llu %lld %d %zu %s\n", HexEncode(entry.path).c_str(),
              static_cast<unsigned long long>(entry.size), static_cast<long long>(entry.mtime),
              entry.isAnchor ? 1 : 0, entry.documents, HexEncode(entry.reason).c_str());
    }
  }

  const int fd = fileno(fp);
  fflush(fp);
  fsync(fd);
  const bool closeOk = fclose(fp) == 0;
  if (!closeOk || rename(tmpPath.c_str(), finalPath.c_str()) != 0)
  {
    unlink(tmpPath.c_str());
    return false;
  }
  return true;
}

void MarkCheckpointComplete(CheckpointState& state, const ShardFile& file, size_t documents,
                            const std::vector<std::string>& outputs)
{
  CheckpointEntry* entry = FindCheckpointEntry(state, file.pathStd);
  if (!entry)
  {
    CheckpointEntry created;
    created.path = file.pathStd;
    state.entries.push_back(created);
    entry = &state.entries.back();
  }
  entry->size = file.size;
  entry->mtime = file.mtime;
  entry->isAnchor = file.isAnchor;
  entry->documents = documents;
  entry->completed = true;
  entry->failed = false;
  entry->reason.clear();
  entry->outputs = outputs;
}

void MarkCheckpointFailed(CheckpointState& state, const ShardFile& file, const std::string& reason)
{
  CheckpointEntry* entry = FindCheckpointEntry(state, file.pathStd);
  if (!entry)
  {
    CheckpointEntry created;
    created.path = file.pathStd;
    state.entries.push_back(created);
    entry = &state.entries.back();
  }
  entry->size = file.size;
  entry->mtime = file.mtime;
  entry->isAnchor = file.isAnchor;
  entry->documents = 0;
  entry->completed = false;
  entry->failed = true;
  entry->reason = reason;
  entry->outputs.clear();
}

bool CompletedOutputsExist(const CheckpointEntry& entry)
{
  if (!entry.completed)
    return false;
  for (const std::string& output : entry.outputs)
  {
    if (!FileExists(output))
      return false;
  }
  return true;
}

bool CheckpointFailedSameInput(const CheckpointState& checkpoint, const ShardFile& file)
{
  const CheckpointEntry* entry = FindCheckpointEntry(checkpoint, file.pathStd);
  return entry && entry->failed && SameFingerprint(*entry, file);
}

bool CheckpointCompletedSameInput(const CheckpointState& checkpoint, const ShardFile& file)
{
  const CheckpointEntry* entry = FindCheckpointEntry(checkpoint, file.pathStd);
  return entry && SameFingerprint(*entry, file) && CompletedOutputsExist(*entry);
}

void RemoveShardOutputs(const avengine::string& outputDir, const ShardFile& file)
{
  DIR* dir = opendir(outputDir.cstr());
  if (!dir)
    return;

  while (dirent* ent = readdir(dir))
  {
    const std::string name(ent->d_name);
    bool belongsToShard = false;
    for (size_t tier = 0; tier < kTierCount && !belongsToShard; ++tier)
    {
      std::string prefix = "tier" + std::to_string(tier);
      if (file.isAnchor)
        prefix += "_anchor";
      prefix += "_";
      prefix += file.stem;
      prefix += "_";
      belongsToShard = name.starts_with(prefix);
    }
    if (belongsToShard && (name.ends_with(".sfi") || name.ends_with(".sfi.tmp")))
    {
      unlink(JoinPath(outputDir, ToAvString(name)).cstr());
    }
  }
  closedir(dir);
}

class TierShardWriter
{
public:
  TierShardWriter(avengine::string outputDir, bool anchor, std::string stem,
                  std::vector<std::string>* outputs)
      : outputDir_(outputDir), anchor_(anchor), stem_(std::move(stem)), outputs_(outputs)
  {
    for (size_t i = 0; i < kTierCount; ++i)
      indexes_[i] = new Index();
  }

  ~TierShardWriter()
  {
    for (Index* index : indexes_)
      delete index;
  }

  bool AddDocument(int tier, const DocMeta& doc, const avengine::vector<TokenAtPosition>& tokens,
                   const avengine::vector<TermEntry>& terms)
  {
    if (tier < 0 || tier >= static_cast<int>(kTierCount))
      tier = 2;

    Index* index = indexes_[tier];
    DocumentMetadata metadata;
    metadata.url = avengine::string(doc.url.cstr());
    metadata.title = avengine::string(doc.title.cstr());
    metadata.description = avengine::string(doc.description.cstr());
    metadata.input = doc.input;

    index->beginDocument(metadata);
    for (const TokenAtPosition& token : tokens)
    {
      if (token.termIndex < terms.size())
        index->ingest(avengine::string_slice(terms[token.termIndex].term.cstr()));
    }
    index->endDocument();

    if (index->offset >= OFFSET_THRESHOLD)
      return FlushTier(static_cast<size_t>(tier));
    return true;
  }

  bool Finish()
  {
    bool ok = true;
    for (size_t tier = 0; tier < kTierCount; ++tier)
    {
      if (!FlushTier(tier))
        ok = false;
    }
    return ok;
  }

  const std::string& error() const
  {
    return error_;
  }

private:
  avengine::string OutputPath(size_t tier, size_t ordinal, bool temp) const
  {
    char ordinalText[32];
    snprintf(ordinalText, sizeof(ordinalText), "%05zu", ordinal);
    std::string name = "tier" + std::to_string(tier);
    if (anchor_)
      name += "_anchor";
    name += "_";
    name += stem_;
    name += "_";
    name += ordinalText;
    name += ".sfi";
    if (temp)
      name += ".tmp";
    return JoinPath(outputDir_, ToAvString(name));
  }

  bool FlushTier(size_t tier)
  {
    Index* index = indexes_[tier];
    if (index->documentMetadata.empty())
      return true;

    const size_t ordinal = counters_[tier]++;
    const avengine::string tmpPath = OutputPath(tier, ordinal, true);
    const avengine::string finalPath = OutputPath(tier, ordinal, false);
    const int fd = open(tmpPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
      error_ = "open output failed";
      perror(tmpPath.cstr());
      return false;
    }

    const bool ioError = index->serialize(fd);
    const bool syncOk = fsync(fd) == 0;
    const bool closeOk = close(fd) == 0;
    if (ioError || !syncOk || !closeOk || rename(tmpPath.cstr(), finalPath.cstr()) != 0)
    {
      error_ = "write output failed";
      unlink(tmpPath.cstr());
      return false;
    }

    if (outputs_)
      outputs_->push_back(ToStdString(finalPath));

    delete indexes_[tier];
    indexes_[tier] = new Index();
    return true;
  }

  avengine::string outputDir_;
  bool anchor_ = false;
  std::string stem_;
  std::vector<std::string>* outputs_ = nullptr;
  Index* indexes_[kTierCount] = {};
  size_t counters_[kTierCount] = {};
  std::string error_;
};

struct RankWorkerArgs
{
  const std::vector<ShardFile>* files = nullptr;
  avengine::atomic_counter* nextFile = nullptr;
  std::vector<RankedDoc>* docs = nullptr;
  avengine::mutex* docsMutex = nullptr;
  ProgressState* progress = nullptr;
  CheckpointState* checkpoint = nullptr;
  avengine::mutex* checkpointMutex = nullptr;
};

void* RankWorkerMain(void* raw)
{
  RankWorkerArgs* args = static_cast<RankWorkerArgs*>(raw);
  std::vector<RankedDoc> localDocs;

  for (;;)
  {
    const int next = args->nextFile->fetch_add(1);
    if (next < 0 || static_cast<size_t>(next) >= args->files->size())
      break;

    const ShardFile& file = (*args->files)[static_cast<size_t>(next)];
    if (file.isAnchor)
      continue;

    ParsedShard shard;
    std::string reason;
    if (!ParseShard(file, shard, reason))
    {
      fprintf(stderr, "warning: failed to parse primary shard %s: %s\n", file.path.cstr(),
              reason.c_str());
      args->progress->failedShards.fetch_add(1);
      args->progress->processedShards.fetch_add(1);
      args->checkpointMutex->lock();
      MarkCheckpointFailed(*args->checkpoint, file, reason);
      args->checkpointMutex->unlock();
      continue;
    }

    for (size_t i = 0; i < shard.docs.size(); ++i)
      localDocs.push_back({shard.docs[i].url, StaticRankScore(shard.docs[i])});

    if (!localDocs.empty())
    {
      args->docsMutex->lock();
      for (const RankedDoc& doc : localDocs)
        args->docs->push_back(doc);
      args->docsMutex->unlock();
      localDocs.clear();
    }
    args->progress->documents.fetch_add(static_cast<int>(shard.docs.size()));
    args->progress->processedShards.fetch_add(1);
  }
  return nullptr;
}

bool BuildRankBoundaries(const std::vector<ShardFile>& files, CheckpointState& checkpoint,
                         RankBoundaries& boundaries, size_t threadCount)
{
  std::vector<RankedDoc> docs;
  const size_t primaryCount = CountPrimaryFiles(files);
  if (primaryCount == 0)
    return false;

  ProgressState progress;
  progress.phase = "ranking";
  progress.totalShards = primaryCount;

  avengine::thread progressThread;
  progressThread.start(ProgressThread, &progress);

  avengine::atomic_counter nextFile(0);
  avengine::mutex docsMutex;
  avengine::mutex checkpointMutex;
  const size_t workerCount = ClampPhaseThreadCount(threadCount, files.size(), kMaxRankThreads);
  printf("[tiered_shard_preprocessor] ranking with %zu worker thread(s)\n", workerCount);
  avengine::thread* workers = new avengine::thread[workerCount];
  RankWorkerArgs* args = new RankWorkerArgs[workerCount];

  bool started = true;
  for (size_t i = 0; i < workerCount; ++i)
  {
    args[i].files = &files;
    args[i].nextFile = &nextFile;
    args[i].docs = &docs;
    args[i].docsMutex = &docsMutex;
    args[i].progress = &progress;
    args[i].checkpoint = &checkpoint;
    args[i].checkpointMutex = &checkpointMutex;
    if (!workers[i].start(RankWorkerMain, &args[i]))
    {
      started = false;
      break;
    }
  }

  for (size_t i = 0; i < workerCount; ++i)
  {
    if (workers[i].joinable())
      workers[i].join();
  }
  progress.done.store(1);
  progressThread.join();

  delete[] args;
  delete[] workers;

  if (!started)
  {
    fprintf(stderr, "failed to start rank worker thread\n");
    return false;
  }
  if (docs.empty())
    return false;

  std::sort(docs.begin(), docs.end(), [](const RankedDoc& lhs, const RankedDoc& rhs) {
    if (lhs.score != rhs.score)
      return lhs.score > rhs.score;
    return lhs.url < rhs.url;
  });

  size_t tier0Index = 0;
  size_t tier1Index = docs.size() - 1u;
  for (size_t i = 0; i < docs.size(); ++i)
  {
    const int tier = TierForRankIndex(i, docs.size());
    if (tier == 0)
      tier0Index = i;
    if (tier <= 1)
      tier1Index = i;
  }

  boundaries.totalDocs = docs.size();
  boundaries.tier0Last = docs[tier0Index];
  boundaries.tier1Last = docs[tier1Index];
  printf("[tiered_shard_preprocessor] ranked %zu primary document(s)\n", docs.size());
  return true;
}

struct TermCursor
{
  const TermEntry* term = nullptr;
  const uint8_t* cursor = nullptr;
  const uint8_t* end = nullptr;
  uint64_t remaining = 0;
  uint32_t current = 0;
  bool valid = false;

  bool advance()
  {
    if (remaining == 0)
    {
      valid = false;
      return true;
    }
    uint32_t delta = 0;
    if (!ReadVarU32(cursor, end, delta) || delta > UINT32_MAX - current)
      return false;
    current += delta;
    --remaining;
    valid = true;
    return true;
  }
};

bool InitTermCursor(const ParsedShard& shard, const TermEntry& term, TermCursor& cursor)
{
  if (!HasBytes(term.postingStart, 24u, shard.mapping.size) || term.postingEnd > shard.mapping.size)
    return false;

  const uint8_t* begin = shard.mapping.data + term.postingStart;
  const uint8_t* end = shard.mapping.data + term.postingEnd;
  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
  const uint8_t* deltas = nullptr;
  if (!SkipPostingHeaderAndSkips(begin, end, documentFrequency, collectionFrequency, skipCount,
                                 deltas))
  {
    return false;
  }
  if (documentFrequency != term.documentFrequency || collectionFrequency != term.positionsCount ||
      collectionFrequency > static_cast<uint64_t>(end - deltas))
  {
    return false;
  }

  cursor.term = &term;
  cursor.cursor = deltas;
  cursor.end = end;
  cursor.remaining = collectionFrequency;
  cursor.current = 0;
  cursor.valid = false;
  return cursor.advance();
}

struct RewriteResult
{
  bool ok = false;
  bool fatal = false;
  size_t documents = 0;
  std::string reason;
  std::vector<std::string> outputs;
};

RewriteResult RewriteShard(const ParsedShard& shard, const RankBoundaries& boundaries,
                           TierShardWriter& writer)
{
  RewriteResult result;
  std::vector<TermCursor> cursors;
  cursors.reserve(shard.terms.size());

  for (size_t termIndex = 0; termIndex < shard.terms.size(); ++termIndex)
  {
    const TermEntry& term = shard.terms[termIndex];
    if (term.term == DOCEND_TOKEN)
      continue;
    TermCursor cursor;
    if (!InitTermCursor(shard, term, cursor))
    {
      result.reason = "invalid term posting";
      return result;
    }
    cursors.push_back(cursor);
  }

  for (size_t chunkBegin = 0; chunkBegin < shard.docs.size(); chunkBegin += kRewriteDocsPerChunk)
  {
    size_t chunkEnd = chunkBegin + kRewriteDocsPerChunk;
    if (chunkEnd > shard.docs.size())
      chunkEnd = shard.docs.size();

    avengine::vector<avengine::vector<TokenAtPosition>> tokensByDoc(chunkEnd - chunkBegin);
    const uint32_t chunkStartPosition =
        chunkBegin == 0 ? 0u : static_cast<uint32_t>(shard.docEndPositions[chunkBegin - 1] + 1u);
    const uint32_t chunkEndPosition = shard.docEndPositions[chunkEnd - 1];

    for (TermCursor& cursor : cursors)
    {
      while (cursor.valid && cursor.current < chunkStartPosition)
      {
        if (!cursor.advance())
        {
          result.reason = "invalid term delta";
          return result;
        }
      }

      size_t docIndex = chunkBegin;
      while (cursor.valid && cursor.current <= chunkEndPosition)
      {
        while (docIndex < chunkEnd && shard.docEndPositions[docIndex] < cursor.current)
          ++docIndex;
        if (docIndex >= chunkEnd)
          break;
        const size_t termIndex = static_cast<size_t>(cursor.term - shard.terms.begin());
        tokensByDoc[docIndex - chunkBegin].pushBack(
            {cursor.current, static_cast<uint32_t>(termIndex)});
        if (!cursor.advance())
        {
          result.reason = "invalid term delta";
          return result;
        }
      }
    }

    for (size_t docIndex = chunkBegin; docIndex < chunkEnd; ++docIndex)
    {
      avengine::vector<TokenAtPosition>& tokens = tokensByDoc[docIndex - chunkBegin];
      tokens.sort([](const TokenAtPosition& lhs, const TokenAtPosition& rhs) {
        if (lhs.position != rhs.position)
          return lhs.position < rhs.position;
        return lhs.termIndex < rhs.termIndex;
      });

      const int tier = TierForDoc(shard.docs[docIndex], boundaries);
      if (!writer.AddDocument(tier, shard.docs[docIndex], tokens, shard.terms))
      {
        result.reason = writer.error();
        result.fatal = true;
        return result;
      }
      ++result.documents;
    }
  }

  if (!writer.Finish())
  {
    result.reason = writer.error();
    result.fatal = true;
    return result;
  }

  result.ok = true;
  return result;
}

struct RewriteWorkerArgs
{
  const std::vector<ShardFile>* files = nullptr;
  const RankBoundaries* boundaries = nullptr;
  CheckpointState* checkpoint = nullptr;
  avengine::mutex* checkpointMutex = nullptr;
  avengine::atomic_counter* nextFile = nullptr;
  ProgressState* progress = nullptr;
  const avengine::string* outputDir = nullptr;
  const avengine::string* checkpointPath = nullptr;
  avengine::atomic_counter* fatalFailure = nullptr;
  avengine::atomic_counter* completedPrimary = nullptr;
};

void* RewriteWorkerMain(void* raw)
{
  RewriteWorkerArgs* args = static_cast<RewriteWorkerArgs*>(raw);

  for (;;)
  {
    if (args->fatalFailure->load())
      break;

    const int next = args->nextFile->fetch_add(1);
    if (next < 0 || static_cast<size_t>(next) >= args->files->size())
      break;

    const ShardFile& file = (*args->files)[static_cast<size_t>(next)];

    args->checkpointMutex->lock();
    const bool completed = CheckpointCompletedSameInput(*args->checkpoint, file);
    const bool failed = CheckpointFailedSameInput(*args->checkpoint, file);
    args->checkpointMutex->unlock();

    if (completed)
    {
      if (!file.isAnchor)
        args->completedPrimary->fetch_add(1);
      args->progress->processedShards.fetch_add(1);
      continue;
    }
    if (failed)
    {
      args->progress->failedShards.fetch_add(1);
      args->progress->processedShards.fetch_add(1);
      continue;
    }

    RemoveShardOutputs(*args->outputDir, file);

    ParsedShard shard;
    std::string reason;
    if (!ParseShard(file, shard, reason))
    {
      fprintf(stderr, "warning: failed to parse shard %s: %s\n", file.path.cstr(), reason.c_str());
      args->checkpointMutex->lock();
      MarkCheckpointFailed(*args->checkpoint, file, reason);
      SaveCheckpoint(*args->checkpointPath, *args->checkpoint);
      args->checkpointMutex->unlock();
      args->progress->failedShards.fetch_add(1);
      args->progress->processedShards.fetch_add(1);
      continue;
    }

    std::vector<std::string> outputs;
    TierShardWriter writer(*args->outputDir, file.isAnchor, file.stem, &outputs);
    RewriteResult result = RewriteShard(shard, *args->boundaries, writer);
    if (!result.ok)
    {
      fprintf(stderr, "warning: failed while rewriting shard %s: %s\n", file.path.cstr(),
              result.reason.c_str());
      args->checkpointMutex->lock();
      if (result.fatal)
      {
        args->fatalFailure->store(1);
      }
      else
      {
        MarkCheckpointFailed(*args->checkpoint, file, result.reason);
        SaveCheckpoint(*args->checkpointPath, *args->checkpoint);
      }
      args->checkpointMutex->unlock();
      args->progress->failedShards.fetch_add(1);
      args->progress->processedShards.fetch_add(1);
      if (result.fatal)
        break;
      continue;
    }

    args->checkpointMutex->lock();
    MarkCheckpointComplete(*args->checkpoint, file, result.documents, outputs);
    SaveCheckpoint(*args->checkpointPath, *args->checkpoint);
    args->checkpointMutex->unlock();

    if (!file.isAnchor)
      args->completedPrimary->fetch_add(1);
    args->progress->documents.fetch_add(static_cast<int>(result.documents));
    args->progress->processedShards.fetch_add(1);
  }
  return nullptr;
}

bool RewriteAll(const std::vector<ShardFile>& files, const RankBoundaries& boundaries,
                CheckpointState& checkpoint, const avengine::string& outputDir,
                const avengine::string& checkpointPath, size_t threadCount)
{
  ProgressState progress;
  progress.phase = "rewrite";
  progress.totalShards = files.size();

  avengine::thread progressThread;
  progressThread.start(ProgressThread, &progress);

  avengine::atomic_counter nextFile(0);
  avengine::atomic_counter fatalFailure(0);
  avengine::atomic_counter completedPrimary(0);
  avengine::mutex checkpointMutex;
  const size_t workerCount = ClampPhaseThreadCount(threadCount, files.size(), kMaxRewriteThreads);
  printf("[tiered_shard_preprocessor] rewriting with %zu worker thread(s)\n", workerCount);
  avengine::thread* workers = new avengine::thread[workerCount];
  RewriteWorkerArgs* args = new RewriteWorkerArgs[workerCount];

  bool started = true;
  for (size_t i = 0; i < workerCount; ++i)
  {
    args[i].files = &files;
    args[i].boundaries = &boundaries;
    args[i].checkpoint = &checkpoint;
    args[i].checkpointMutex = &checkpointMutex;
    args[i].nextFile = &nextFile;
    args[i].progress = &progress;
    args[i].outputDir = &outputDir;
    args[i].checkpointPath = &checkpointPath;
    args[i].fatalFailure = &fatalFailure;
    args[i].completedPrimary = &completedPrimary;
    if (!workers[i].start(RewriteWorkerMain, &args[i]))
    {
      started = false;
      fatalFailure.store(1);
      break;
    }
  }

  for (size_t i = 0; i < workerCount; ++i)
  {
    if (workers[i].joinable())
      workers[i].join();
  }
  progress.done.store(1);
  progressThread.join();

  delete[] args;
  delete[] workers;

  if (!started)
    fprintf(stderr, "failed to start rewrite worker thread\n");
  if (started && !fatalFailure.load() && completedPrimary.get_val() == 0)
  {
    fprintf(stderr, "[tiered_shard_preprocessor] no primary shards completed\n");
    return false;
  }
  return started && !fatalFailure.load();
}

avengine::string DefaultCheckpointPath(const avengine::string& outputDir)
{
  return JoinPath(outputDir, avengine::string(".tiered_preprocess.checkpoint"));
}

void Usage(const char* prog)
{
  fprintf(stderr,
          "Usage: %s [--input DIR] [--output DIR] [--threads N] [--checkpoint PATH]\n"
          "          [--force] [--reset-checkpoint]\n"
          "Rewrites existing primary and anchor .sfi shards into tiered .sfi shards.\n",
          prog);
}

bool ParseOptions(int argc, char** argv, Options& options)
{
  for (int i = 1; i < argc; ++i)
  {
    const avengine::string arg(argv[i]);
    if (arg == "--input" && i + 1 < argc)
      options.inputDir = argv[++i];
    else if (arg == "--output" && i + 1 < argc)
      options.outputDir = argv[++i];
    else if (arg == "--checkpoint" && i + 1 < argc)
      options.checkpointPath = argv[++i];
    else if (arg == "--threads" && i + 1 < argc)
    {
      if (!ParseSize(argv[++i], options.threads) || options.threads == 0)
      {
        fprintf(stderr, "--threads must be a positive integer\n");
        return false;
      }
    }
    else if (arg == "--force")
      options.force = true;
    else if (arg == "--reset-checkpoint")
      options.resetCheckpoint = true;
    else if (arg == "--help" || arg == "-h")
    {
      Usage(argv[0]);
      options.help = true;
      return true;
    }
    else
    {
      Usage(argv[0]);
      return false;
    }
  }
  if (options.checkpointPath.size() == 0)
    options.checkpointPath = DefaultCheckpointPath(options.outputDir);
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  Options options;
  if (!ParseOptions(argc, argv, options))
    return 1;
  if (options.help)
    return 0;
  if (options.inputDir == options.outputDir)
  {
    fprintf(stderr, "[tiered_shard_preprocessor] input and output directories must differ\n");
    return 1;
  }

  const std::vector<ShardFile> files = DiscoverSfiFiles(options.inputDir);
  if (files.empty())
  {
    fprintf(stderr, "[tiered_shard_preprocessor] no .sfi files found in %s\n",
            options.inputDir.cstr());
    return 1;
  }

  if (!PrepareOutputDirectory(options.outputDir, options.checkpointPath, options.force,
                              options.resetCheckpoint))
  {
    return 1;
  }

  CheckpointState checkpoint;
  if (!options.resetCheckpoint && LoadCheckpoint(options.checkpointPath, checkpoint))
  {
    printf("[tiered_shard_preprocessor] loaded checkpoint %s\n", options.checkpointPath.cstr());
  }

  const size_t threadCount = ClampThreadCount(options.threads, files.size());
  printf("[tiered_shard_preprocessor] found %zu shard(s), max worker thread(s) %zu\n",
         files.size(), threadCount);

  RankBoundaries boundaries;
  if (!BuildRankBoundaries(files, checkpoint, boundaries, threadCount))
  {
    fprintf(stderr, "[tiered_shard_preprocessor] no primary documents found\n");
    SaveCheckpoint(options.checkpointPath, checkpoint);
    return 1;
  }
  SaveCheckpoint(options.checkpointPath, checkpoint);

  if (!RewriteAll(files, boundaries, checkpoint, options.outputDir, options.checkpointPath,
                  threadCount))
  {
    SaveCheckpoint(options.checkpointPath, checkpoint);
    return 1;
  }

  SaveCheckpoint(options.checkpointPath, checkpoint);
  printf("[tiered_shard_preprocessor] wrote tiered shards to %s\n", options.outputDir.cstr());
  return 0;
}
