#include <avengine/codec.h>
#include <avengine/indexer/indexer.h>
#include <avengine/unordered_map.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

constexpr uint64_t kIndexMagic = 0x008FB8EF84979FF0ULL;
constexpr size_t kTierCount = 3;

struct Options
{
  avengine::string inputDir = avengine::string(".");
  avengine::string outputDir = avengine::string("tiered-index");
  bool force = false;
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
  double staticScore = 0.0;
};

struct ParsedShard
{
  avengine::string path;
  bool isAnchor = false;
  avengine::vector<uint8_t> bytes;
  avengine::vector<TermEntry> terms;
  avengine::vector<uint32_t> docEndPositions;
  avengine::vector<DocMeta> docs;
};

struct RankedDoc
{
  avengine::string url;
  double score = 0.0;
  avengine::string sourcePath;
  size_t sourceDocId = 0;
};

struct TokenAtPosition
{
  uint32_t position = 0;
  avengine::string term;
};

bool HasSfiSuffix(const avengine::string& name)
{
  const size_t n = name.size();
  return n >= 4 && name[n - 4] == '.' && name[n - 3] == 's' && name[n - 2] == 'f' &&
         name[n - 1] == 'i';
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

bool ReadFile(const avengine::string& path, avengine::vector<uint8_t>& out)
{
  out.clear();
  const int fd = open(path.cstr(), O_RDONLY);
  if (fd < 0)
    return false;

  struct stat st = {};
  if (fstat(fd, &st) != 0 || st.st_size < 0)
  {
    close(fd);
    return false;
  }

  out.resize(static_cast<size_t>(st.st_size));
  size_t total = 0;
  while (total < out.size())
  {
    const ssize_t n = read(fd, out.begin() + total, out.size() - total);
    if (n <= 0)
    {
      close(fd);
      return false;
    }
    total += static_cast<size_t>(n);
  }

  close(fd);
  return true;
}

uint64_t ReadU64Le(const uint8_t* p)
{
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

void CopyBytes(void* destination, const void* source, size_t size)
{
  uint8_t* dst = static_cast<uint8_t*>(destination);
  const uint8_t* src = static_cast<const uint8_t*>(source);
  for (size_t i = 0; i < size; ++i)
    dst[i] = src[i];
}

bool ReadLpString(const uint8_t*& cursor, const uint8_t* end, avengine::string& out)
{
  if (cursor + sizeof(uint64_t) > end)
    return false;
  const uint64_t len = ReadU64Le(cursor);
  cursor += sizeof(uint64_t);
  if (len > static_cast<uint64_t>(end - cursor))
    return false;
  out = avengine::string();
  for (size_t i = 0; i < static_cast<size_t>(len); ++i)
    out.pushBack(static_cast<char>(cursor[i]));
  cursor += len;
  return true;
}

bool ReadVarU32(const uint8_t*& cursor, const uint8_t* end, uint32_t& out)
{
  uint32_t result = 0;
  uint32_t shift = 0;
  while (cursor < end && shift < 35)
  {
    const uint8_t byte = *cursor++;
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

bool DecodePositions(const ParsedShard& shard, const TermEntry& term, avengine::vector<uint32_t>& out)
{
  out.clear();
  if (term.postingStart + 24u > term.postingEnd || term.postingEnd > shard.bytes.size())
    return false;

  const uint8_t* cursor = shard.bytes.begin() + term.postingStart;
  const uint8_t* end = shard.bytes.begin() + term.postingEnd;
  const uint64_t collectionFrequency = ReadU64Le(cursor + 8);
  const uint64_t skipCount = ReadU64Le(cursor + 16);
  cursor += 24;

  uint32_t ignored = 0;
  for (uint64_t s = 0; s < skipCount; ++s)
  {
    for (int field = 0; field < 3; ++field)
    {
      if (!ReadVarU32(cursor, end, ignored))
        return false;
    }
  }

  uint32_t absolute = 0;
  out.reserve(static_cast<size_t>(collectionFrequency));
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    uint32_t delta = 0;
    if (!ReadVarU32(cursor, end, delta))
      return false;
    absolute += delta;
    out.pushBack(absolute);
  }
  return true;
}

bool ParseDocEnd(ParsedShard& shard)
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
    return false;

  if (!DecodePositions(shard, *docEnd, shard.docEndPositions))
    return false;

  const uint8_t* cursor = shard.bytes.begin() + docEnd->postingStart;
  const uint8_t* end = shard.bytes.begin() + docEnd->postingEnd;
  const uint64_t documentFrequency = ReadU64Le(cursor);
  const uint64_t collectionFrequency = ReadU64Le(cursor + 8);
  const uint64_t skipCount = ReadU64Le(cursor + 16);
  cursor += 24;

  uint32_t ignored = 0;
  for (uint64_t s = 0; s < skipCount; ++s)
  {
    for (int field = 0; field < 3; ++field)
    {
      if (!ReadVarU32(cursor, end, ignored))
        return false;
    }
  }
  for (uint64_t i = 0; i < collectionFrequency; ++i)
  {
    if (!ReadVarU32(cursor, end, ignored))
      return false;
  }

  shard.docs.clear();
  shard.docs.reserve(static_cast<size_t>(documentFrequency));
  for (uint64_t i = 0; i < documentFrequency; ++i)
  {
    DocMeta doc;
    if (!ReadLpString(cursor, end, doc.url) || !ReadLpString(cursor, end, doc.title) ||
        !ReadLpString(cursor, end, doc.description))
    {
      return false;
    }
    if (cursor + sizeof(RankingInput) > end)
      return false;
    CopyBytes(&doc.input, cursor, sizeof(RankingInput));
    cursor += sizeof(RankingInput);
    shard.docs.pushBack(doc);
  }

  return shard.docs.size() == shard.docEndPositions.size();
}

bool ParseShard(const avengine::string& path, bool isAnchor, ParsedShard& shard)
{
  shard = ParsedShard();
  shard.path = path;
  shard.isAnchor = isAnchor;
  if (!ReadFile(path, shard.bytes) || shard.bytes.size() < 5u * sizeof(uint64_t))
    return false;

  const uint8_t* data = shard.bytes.begin();
  const uint8_t* end = data + shard.bytes.size();
  if (ReadU64Le(data) != kIndexMagic)
    return false;
  const uint64_t documentCount = ReadU64Le(data + 8);
  const uint64_t uniqueWords = ReadU64Le(data + 16);
  const uint64_t rankingInputSize = ReadU64Le(data + 32);
  if (rankingInputSize != sizeof(RankingInput))
    return false;
  if (documentCount == 0)
    return true;

  const uint8_t* cursor = data + (5u * sizeof(uint64_t));
  shard.terms.reserve(static_cast<size_t>(uniqueWords));
  for (uint64_t i = 0; i < uniqueWords; ++i)
  {
    TermEntry term;
    if (!ReadLpString(cursor, end, term.term))
      return false;
    if (cursor + 24 > end)
      return false;
    term.positionsCount = ReadU64Le(cursor);
    term.documentFrequency = ReadU64Le(cursor + 8);
    term.postingOffset = ReadU64Le(cursor + 16);
    cursor += 24;
    shard.terms.pushBack(term);
  }

  const size_t postingsStart = static_cast<size_t>(cursor - data);
  avengine::vector<size_t> order(shard.terms.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  order.sort([&](size_t lhs, size_t rhs) {
    return shard.terms[lhs].postingOffset < shard.terms[rhs].postingOffset;
  });

  for (size_t oi = 0; oi < order.size(); ++oi)
  {
    TermEntry& term = shard.terms[order[oi]];
    const size_t start = postingsStart + static_cast<size_t>(term.postingOffset);
    const size_t next = oi + 1 < order.size()
                            ? postingsStart +
                                  static_cast<size_t>(shard.terms[order[oi + 1]].postingOffset)
                            : shard.bytes.size();
    if (start > next || next > shard.bytes.size())
      return false;
    term.postingStart = start;
    term.postingEnd = next;
  }

  if (!ParseDocEnd(shard))
    return false;

  for (DocMeta& doc : shard.docs)
    doc.staticScore = 0.0;
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

int TierForScore(double score, double tier0Min, double tier1Min)
{
  if (score >= tier0Min)
    return 0;
  if (score >= tier1Min)
    return 1;
  return 2;
}

bool EnsureOutputDirectory(const avengine::string& path)
{
  struct stat st = {};
  if (stat(path.cstr(), &st) == 0)
    return (st.st_mode & S_IFDIR) != 0;
  return mkdir(path.cstr(), 0755) == 0;
}

bool PrepareOutputDirectory(const avengine::string& path, bool force)
{
  if (!EnsureOutputDirectory(path))
  {
    fprintf(stderr, "failed to create output directory: %s\n", path.cstr());
    return false;
  }

  DIR* dir = opendir(path.cstr());
  if (!dir)
    return false;

  avengine::vector<avengine::string> sfiFiles;
  avengine::vector<avengine::string> cacheFiles;
  while (dirent* ent = readdir(dir))
  {
    const avengine::string name(ent->d_name);
    if (HasSfiSuffix(name))
    {
      sfiFiles.pushBack(JoinPath(path, name));
      avengine::string cacheName(name);
      cacheName += avengine::string(".loadcache");
      cacheFiles.pushBack(JoinPath(path, cacheName));
    }
  }
  closedir(dir);

  if (!force && !sfiFiles.empty())
  {
    fprintf(stderr, "output directory already contains .sfi files; pass --force to replace\n");
    return false;
  }

  for (const avengine::string& file : sfiFiles)
    unlink(file.cstr());
  for (const avengine::string& file : cacheFiles)
    unlink(file.cstr());
  return true;
}

avengine::vector<avengine::string> DiscoverSfiFiles(const avengine::string& dirPath)
{
  avengine::vector<avengine::string> files;
  DIR* dir = opendir(dirPath.cstr());
  if (!dir)
    return files;

  while (dirent* ent = readdir(dir))
  {
    if (ent->d_name[0] == '.')
      continue;
    const avengine::string name(ent->d_name);
    if (HasSfiSuffix(name))
      files.pushBack(JoinPath(dirPath, name));
  }
  closedir(dir);
  files.sort();
  return files;
}

class TierShardWriter
{
public:
  TierShardWriter(avengine::string outputDir, bool anchor)
      : outputDir_(outputDir), anchor_(anchor)
  {
    for (size_t i = 0; i < kTierCount; ++i)
      indexes_[i] = new Index();
  }

  ~TierShardWriter()
  {
    for (Index* index : indexes_)
      delete index;
  }

  bool AddDocument(int tier, const DocMeta& doc, const avengine::vector<TokenAtPosition>& tokens)
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
      index->ingest(avengine::string_slice(token.term.cstr()));
    index->endDocument();

    if (index->offset >= OFFSET_THRESHOLD)
      return FlushTier(static_cast<size_t>(tier), false);
    return true;
  }

  bool Finish()
  {
    bool ok = true;
    for (size_t tier = 0; tier < kTierCount; ++tier)
    {
      if (!FlushTier(tier, true))
        ok = false;
    }
    return ok;
  }

private:
  avengine::string OutputPath(size_t tier, size_t ordinal, bool temp) const
  {
    char name[64];
    if (anchor_)
      snprintf(name, sizeof(name), "tier%zu_anchor_%05zu.sfi%s", tier, ordinal,
                    temp ? ".tmp" : "");
    else
      snprintf(name, sizeof(name), "tier%zu_%05zu.sfi%s", tier, ordinal,
                    temp ? ".tmp" : "");
    return JoinPath(outputDir_, name);
  }

  bool FlushTier(size_t tier, bool final)
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
      perror(tmpPath.cstr());
      return false;
    }

    const bool ioError = index->serialize(fd);
    const bool closeOk = close(fd) == 0;
    if (ioError || !closeOk || rename(tmpPath.cstr(), finalPath.cstr()) != 0)
    {
      unlink(tmpPath.cstr());
      return false;
    }

    delete indexes_[tier];
    indexes_[tier] = final ? new Index() : new Index();
    return true;
  }

  avengine::string outputDir_;
  bool anchor_ = false;
  Index* indexes_[kTierCount] = {};
  size_t counters_[kTierCount] = {};
};

bool BuildUrlTierMap(const avengine::vector<avengine::string>& files,
                     avengine::unordered_map<int>& tierByUrl, double& tier0Min,
                     double& tier1Min)
{
  avengine::vector<RankedDoc> docs;
  for (const avengine::string& path : files)
  {
    if (IsAnchorShardName(path))
      continue;

    ParsedShard shard;
    if (!ParseShard(path, false, shard))
    {
      fprintf(stderr, "warning: failed to parse primary shard %s\n", path.cstr());
      continue;
    }

    for (size_t i = 0; i < shard.docs.size(); ++i)
    {
      const DocMeta& doc = shard.docs[i];
      docs.pushBack({doc.url, StaticRankScore(doc), path, i});
    }
  }

  if (docs.empty())
    return false;

  docs.sort([](const RankedDoc& lhs, const RankedDoc& rhs) {
    if (lhs.score != rhs.score)
      return lhs.score > rhs.score;
    if (lhs.url != rhs.url)
      return lhs.url < rhs.url;
    if (lhs.sourcePath != rhs.sourcePath)
      return lhs.sourcePath < rhs.sourcePath;
    return lhs.sourceDocId < rhs.sourceDocId;
  });

  tier0Min = docs.front().score;
  tier1Min = docs.back().score;
  for (size_t i = 0; i < docs.size(); ++i)
  {
    const int tier = TierForRankIndex(i, docs.size());
    if (tier == 0)
      tier0Min = docs[i].score;
    else if (tier == 1)
      tier1Min = docs[i].score;

    if (!tierByUrl.get(avengine::string_slice(docs[i].url.cstr())))
      tierByUrl.insert(docs[i].url, tier);
  }

  printf("[tiered_shard_preprocessor] ranked %zu primary document(s)\n", docs.size());
  return true;
}

bool RewriteShard(const ParsedShard& shard, const avengine::unordered_map<int>& tierByUrl,
                  double tier0Min, double tier1Min, TierShardWriter& writer)
{
  avengine::vector<avengine::vector<TokenAtPosition>> tokensByDoc(shard.docs.size());
  avengine::vector<uint32_t> positions;

  for (const TermEntry& term : shard.terms)
  {
    if (term.term == DOCEND_TOKEN)
      continue;
    if (!DecodePositions(shard, term, positions))
      return false;

    for (uint32_t pos : positions)
    {
      size_t docIndex = 0;
      size_t hi = shard.docEndPositions.size();
      while (docIndex < hi)
      {
        const size_t mid = docIndex + ((hi - docIndex) / 2);
        if (shard.docEndPositions[mid] < pos)
          docIndex = mid + 1;
        else
          hi = mid;
      }
      if (docIndex >= shard.docEndPositions.size())
        continue;
      if (docIndex < tokensByDoc.size())
        tokensByDoc[docIndex].pushBack({pos, term.term});
    }
  }

  for (size_t docIndex = 0; docIndex < shard.docs.size(); ++docIndex)
  {
    avengine::vector<TokenAtPosition>& tokens = tokensByDoc[docIndex];
    tokens.sort([](const TokenAtPosition& lhs, const TokenAtPosition& rhs) {
      if (lhs.position != rhs.position)
        return lhs.position < rhs.position;
      return lhs.term < rhs.term;
    });

    int tier = 2;
    const int* found = tierByUrl.get(avengine::string_slice(shard.docs[docIndex].url.cstr()));
    if (found)
      tier = *found;
    else
      tier = TierForScore(StaticRankScore(shard.docs[docIndex]), tier0Min, tier1Min);

    if (!writer.AddDocument(tier, shard.docs[docIndex], tokens))
      return false;
  }

  return true;
}

bool RewriteAll(const avengine::vector<avengine::string>& files,
                const avengine::unordered_map<int>& tierByUrl, double tier0Min,
                double tier1Min, const avengine::string& outputDir)
{
  TierShardWriter primaryWriter(outputDir, false);
  TierShardWriter anchorWriter(outputDir, true);

  for (const avengine::string& path : files)
  {
    const bool isAnchor = IsAnchorShardName(path);
    ParsedShard shard;
    if (!ParseShard(path, isAnchor, shard))
    {
      fprintf(stderr, "warning: failed to parse shard %s\n", path.cstr());
      continue;
    }

    TierShardWriter& writer = isAnchor ? anchorWriter : primaryWriter;
    if (!RewriteShard(shard, tierByUrl, tier0Min, tier1Min, writer))
    {
      fprintf(stderr, "failed while rewriting shard %s\n", path.cstr());
      return false;
    }
  }

  return primaryWriter.Finish() && anchorWriter.Finish();
}

void Usage(const char* prog)
{
  fprintf(stderr,
               "Usage: %s [--input DIR] [--output DIR] [--force]\n"
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
    else if (arg == "--force")
      options.force = true;
    else if (arg == "--help" || arg == "-h")
    {
      Usage(argv[0]);
      return false;
    }
    else
    {
      Usage(argv[0]);
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  Options options;
  if (!ParseOptions(argc, argv, options))
    return 1;
  if (options.inputDir == options.outputDir)
  {
    fprintf(stderr, "[tiered_shard_preprocessor] input and output directories must differ\n");
    return 1;
  }

  const avengine::vector<avengine::string> files = DiscoverSfiFiles(options.inputDir);
  if (files.empty())
  {
    fprintf(stderr, "[tiered_shard_preprocessor] no .sfi files found in %s\n",
                 options.inputDir.cstr());
    return 1;
  }

  if (!PrepareOutputDirectory(options.outputDir, options.force))
    return 1;

  avengine::unordered_map<int> tierByUrl;
  double tier0Min = 0.0;
  double tier1Min = 0.0;
  if (!BuildUrlTierMap(files, tierByUrl, tier0Min, tier1Min))
  {
    fprintf(stderr, "[tiered_shard_preprocessor] no primary documents found\n");
    return 1;
  }

  if (!RewriteAll(files, tierByUrl, tier0Min, tier1Min, options.outputDir))
    return 1;

  printf("[tiered_shard_preprocessor] wrote tiered shards to %s\n", options.outputDir.cstr());
  return 0;
}
