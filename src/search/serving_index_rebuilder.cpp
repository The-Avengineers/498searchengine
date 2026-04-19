#include <avengine/search/serving_index_format.h>
#include <avengine/search/serving_index_rebuilder.h>
#include <avengine/search/legacy_shard_reader.h>
#include <avengine/codec.h>
#include <avengine/unordered_map.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace avengine
{
namespace search
{
namespace
{

bool IsSeparator(char c)
{
  return c == '/';
}

string JoinPath(const string& directory, const string& filename)
{
  if (directory.size() == 0 || directory == string("."))
    return filename;

  string out(directory);
  if (out.size() > 0 && !IsSeparator(out[out.size() - 1]))
    out.pushBack('/');
  out += filename;
  return out;
}

void SetErrnoError(const char* prefix, const string& path, string* error)
{
  if (!error)
    return;
  *error = string(prefix);
  *error += string(": ");
  *error += path;
  *error += string(": ");
  *error += string(std::strerror(errno));
}

bool IsPrimaryShardName(const char* name, uint64_t* shardId)
{
  if (!name)
    return false;

  uint64_t id = 0;
  for (size_t i = 0; i < 5; ++i)
  {
    if (name[i] < '0' || name[i] > '9')
      return false;
    id = (id * 10u) + static_cast<uint64_t>(name[i] - '0');
  }

  if (name[5] != '.' || name[6] != 's' || name[7] != 'f' || name[8] != 'i' ||
      name[9] != '\0')
  {
    return false;
  }

  if (shardId)
    *shardId = id;
  return true;
}

bool IsAnchorShardName(const char* name)
{
  static const char kPrefix[] = "anchor_";
  if (!name)
    return false;

  size_t i = 0;
  for (; kPrefix[i] != '\0'; ++i)
  {
    if (name[i] != kPrefix[i])
      return false;
  }

  uint64_t ignored = 0;
  return IsPrimaryShardName(name + i, &ignored);
}

string ServingOutputName(uint64_t shardId)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "serving_%05llu.ssi",
                static_cast<unsigned long long>(shardId));
  return string(buf);
}

bool WriteAll(int fd, const void* data, size_t bytes)
{
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t written = 0;
  while (written < bytes)
  {
    const ssize_t n = write(fd, p + written, bytes - written);
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    written += static_cast<size_t>(n);
  }
  return true;
}

uint64_t HashTerm(const string& term)
{
  uint64_t hash = 1469598103934665603ULL;
  for (char c : term)
  {
    hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint32_t SaturatingScoreQ16(uint32_t bodyTf, uint32_t titleTf, uint32_t anchorTf)
{
  const uint64_t score = (static_cast<uint64_t>(bodyTf) * 65536ULL) +
                         (static_cast<uint64_t>(titleTf) * 3ULL * 65536ULL) +
                         (static_cast<uint64_t>(anchorTf) * 2ULL * 65536ULL);
  return score > 0xffffffffULL ? 0xffffffffu : static_cast<uint32_t>(score);
}

uint8_t FieldMask(uint32_t bodyTf, uint32_t titleTf, uint32_t anchorTf)
{
  uint8_t mask = 0;
  if (bodyTf > 0)
    mask |= kServingFieldBody;
  if (titleTf > 0)
    mask |= kServingFieldTitle;
  if (anchorTf > 0)
    mask |= kServingFieldAnchor;
  return mask;
}

uint32_t TierForPosting(uint32_t bodyTf, uint32_t titleTf, uint32_t anchorTf)
{
  if (titleTf > 0 || anchorTf > 0)
    return static_cast<uint32_t>(ServingTierId::Strong);
  if (bodyTf >= 2)
    return static_cast<uint32_t>(ServingTierId::Medium);
  return static_cast<uint32_t>(ServingTierId::Recall);
}

struct DocRef
{
  size_t shardIndex = 0;
  uint32_t docId = 0;
};

struct PostingEvidence
{
  uint32_t docId = 0;
  uint32_t bodyTf = 0;
  uint32_t titleTf = 0;
  uint32_t anchorTf = 0;
};

struct TermBuildState
{
  string term;
  vector<PostingEvidence> postings;
};

struct OutputShardState
{
  const ServingShardFile* file = nullptr;
  LegacyShardReader* reader = nullptr;
  vector<TermBuildState> terms;
  vector<uint32_t> docLengths;
};

void DestroyOutputShards(vector<OutputShardState>& shards)
{
  for (OutputShardState& shard : shards)
  {
    delete shard.reader;
    shard.reader = nullptr;
  }
}

TermBuildState* FindOrAddTerm(vector<TermBuildState>& terms, const string& term)
{
  for (TermBuildState& existing : terms)
  {
    if (existing.term == term)
      return &existing;
  }

  TermBuildState fresh;
  fresh.term = term;
  terms.pushBack(fresh);
  return &terms.back();
}

void AddEvidence(vector<TermBuildState>& terms, const string& term, uint32_t docId,
                 uint32_t bodyTf, uint32_t titleTf,
                 uint32_t anchorTf)
{
  TermBuildState* termState = FindOrAddTerm(terms, term);
  for (PostingEvidence& evidence : termState->postings)
  {
    if (evidence.docId == docId)
    {
      evidence.bodyTf += bodyTf;
      evidence.titleTf += titleTf;
      evidence.anchorTf += anchorTf;
      return;
    }
  }

  termState->postings.pushBack({docId, bodyTf, titleTf, anchorTf});
}

void WriteU8(Writer& writer, uint8_t value)
{
  writer.writeBytes(&value, sizeof(value));
}

template <typename T>
void AppendRaw(vector<uint8_t>& out, const T* data, size_t count)
{
  if (!data || count == 0)
    return;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  const size_t bytesCount = sizeof(T) * count;
  out.reserve(out.size() + bytesCount);
  for (size_t i = 0; i < bytesCount; ++i)
    out.pushBack(bytes[i]);
}

void AppendBytes(vector<uint8_t>& out, const void* data, size_t bytes)
{
  if (!data || bytes == 0)
    return;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  out.reserve(out.size() + bytes);
  for (size_t i = 0; i < bytes; ++i)
    out.pushBack(p[i]);
}

bool WriteVector(int fd, const vector<uint8_t>& bytes)
{
  return bytes.empty() || WriteAll(fd, bytes.begin(), bytes.size());
}

bool BuildServingShardBytes(const OutputShardState& shard, uint32_t blockSize,
                            vector<uint8_t>* out, string* error)
{
  if (!shard.reader || !shard.file || !out)
    return false;
  if (blockSize == 0)
  {
    if (error)
      *error = string("block size must be greater than zero");
    return false;
  }

  vector<ServingLexiconEntryV1> lexicon;
  vector<ServingTermInfoV1> termInfos;
  vector<ServingTierInfoV1> tierInfos;
  vector<ServingBlockInfoV1> blockInfos;
  vector<ServingDocMetadataEntryV1> coldDocEntries;
  vector<uint8_t> termBytes;
  vector<uint8_t> postings;
  vector<uint8_t> coldStrings;

  vector<TermBuildState> sortedTerms = shard.terms;
  sortedTerms.sort([](const TermBuildState& a, const TermBuildState& b) {
    return a.term < b.term;
  });

  for (const TermBuildState& termState : sortedTerms)
  {
    const string& term = termState.term;
    if (term.size() == 0 || termState.postings.empty())
      continue;

    ServingLexiconEntryV1 lex = {};
    lex.termOffset = static_cast<uint64_t>(termBytes.size());
    lex.termLength = static_cast<uint32_t>(term.size());
    lex.termInfoIndex = static_cast<uint32_t>(termInfos.size());
    lex.termHash = HashTerm(term);
    AppendBytes(termBytes, term.cstr(), term.size());

    ServingTermInfoV1 termInfo = {};
    termInfo.tierInfoIndex = static_cast<uint32_t>(tierInfos.size());

    vector<PostingEvidence> tiers[kServingTierCount];
    for (const PostingEvidence& sourcePosting : termState.postings)
    {
      PostingEvidence posting = sourcePosting;
      if (posting.bodyTf == 0 && posting.titleTf == 0 && posting.anchorTf == 0)
        continue;
      const uint32_t tierId = TierForPosting(posting.bodyTf, posting.titleTf, posting.anchorTf);
      tiers[tierId].pushBack(posting);
      termInfo.documentFrequency += 1u;
      termInfo.collectionFrequency += posting.bodyTf + posting.titleTf + posting.anchorTf;
      if (posting.bodyTf > 0)
        termInfo.bodyDocumentFrequency += 1u;
      if (posting.titleTf > 0)
        termInfo.titleDocumentFrequency += 1u;
      if (posting.anchorTf > 0)
        termInfo.anchorDocumentFrequency += 1u;
      const uint32_t score = SaturatingScoreQ16(posting.bodyTf, posting.titleTf, posting.anchorTf);
      if (score > termInfo.maxScoreQ16)
        termInfo.maxScoreQ16 = score;
    }

    for (uint32_t tierId = 0; tierId < kServingTierCount; ++tierId)
    {
      vector<PostingEvidence>& tierPostings = tiers[tierId];
      if (tierPostings.empty())
        continue;

      tierPostings.sort([](const PostingEvidence& a, const PostingEvidence& b) {
        return a.docId < b.docId;
      });

      ServingTierInfoV1 tierInfo = {};
      tierInfo.tierId = tierId;
      tierInfo.postingCount = static_cast<uint32_t>(tierPostings.size());
      tierInfo.blockIndex = static_cast<uint32_t>(blockInfos.size());
      tierInfo.payloadOffset = static_cast<uint64_t>(postings.size());

      for (size_t blockStart = 0; blockStart < tierPostings.size(); blockStart += blockSize)
      {
        size_t blockEnd = blockStart + static_cast<size_t>(blockSize);
        if (blockEnd > tierPostings.size())
          blockEnd = tierPostings.size();
        const uint64_t payloadStart = postings.size();
        Writer blockWriter;
        uint32_t previousDocId = 0;
        ServingBlockInfoV1 blockInfo = {};
        blockInfo.firstDocId = tierPostings[blockStart].docId;
        blockInfo.lastDocId = tierPostings[blockEnd - 1u].docId;
        blockInfo.docCount = static_cast<uint32_t>(blockEnd - blockStart);

        for (size_t i = blockStart; i < blockEnd; ++i)
        {
          const PostingEvidence& posting = tierPostings[i];
          const uint32_t delta = i == blockStart ? posting.docId : posting.docId - previousDocId;
          previousDocId = posting.docId;
          const uint8_t mask = FieldMask(posting.bodyTf, posting.titleTf, posting.anchorTf);
          blockWriter.writeVarU32(delta);
          WriteU8(blockWriter, mask);
          if ((mask & kServingFieldBody) != 0)
            blockWriter.writeVarU32(posting.bodyTf);
          if ((mask & kServingFieldTitle) != 0)
            blockWriter.writeVarU32(posting.titleTf);
          if ((mask & kServingFieldAnchor) != 0)
            blockWriter.writeVarU32(posting.anchorTf);

          const uint32_t score = SaturatingScoreQ16(posting.bodyTf, posting.titleTf,
                                                    posting.anchorTf);
          if (score > blockInfo.maxScoreQ16)
            blockInfo.maxScoreQ16 = score;
          if (posting.bodyTf > blockInfo.maxBodyTf)
            blockInfo.maxBodyTf = posting.bodyTf;
          if (posting.titleTf > blockInfo.maxTitleTf)
            blockInfo.maxTitleTf = posting.titleTf;
          if (posting.anchorTf > blockInfo.maxAnchorTf)
            blockInfo.maxAnchorTf = posting.anchorTf;
          blockInfo.fieldMaskUnion |= mask;
        }

        const size_t oldSize = postings.size();
        postings.resize(oldSize + blockWriter.size());
        if (blockWriter.size() > 0)
          std::memcpy(postings.begin() + oldSize, blockWriter.data(), blockWriter.size());

        blockInfo.payloadOffset = payloadStart;
        blockInfo.payloadLength = postings.size() - payloadStart;
        blockInfos.pushBack(blockInfo);
        if (blockInfo.maxScoreQ16 > tierInfo.maxScoreQ16)
          tierInfo.maxScoreQ16 = blockInfo.maxScoreQ16;
      }

      tierInfo.blockCount = static_cast<uint32_t>(blockInfos.size() - tierInfo.blockIndex);
      tierInfo.payloadLength = static_cast<uint64_t>(postings.size()) - tierInfo.payloadOffset;
      tierInfos.pushBack(tierInfo);
      termInfo.tierCount += 1u;
    }

    if (termInfo.documentFrequency == 0)
      continue;

    lexicon.pushBack(lex);
    termInfos.pushBack(termInfo);
  }

  ServingShardHeaderV1 header = MakeEmptyServingShardHeaderV1(shard.file->shardId);
  header.blockSize = blockSize;
  header.documentCount = shard.reader->DocumentCount();
  header.lexiconEntryCount = lexicon.size();
  header.termCount = termInfos.size();
  header.tierCount = tierInfos.size();
  header.blockCount = blockInfos.size();
  header.flags = kServingShardHasColdPayload;

  coldDocEntries.reserve(shard.reader->DocumentCount());
  const uint32_t coldStringBase =
      static_cast<uint32_t>(shard.reader->DocumentCount() * sizeof(ServingDocMetadataEntryV1));
  for (size_t doc = 0; doc < shard.reader->DocumentCount(); ++doc)
  {
    const LegacyDocumentMetadata* meta = shard.reader->GetDocument(static_cast<uint32_t>(doc));
    if (!meta)
    {
      if (error)
        *error = string("document metadata missing while writing cold payload");
      return false;
    }

    ServingDocMetadataEntryV1 entry = {};
    entry.urlOffset = coldStringBase + static_cast<uint32_t>(coldStrings.size());
    entry.urlLength = static_cast<uint32_t>(meta->url.size());
    AppendBytes(coldStrings, meta->url.cstr(), meta->url.size());
    entry.titleOffset = coldStringBase + static_cast<uint32_t>(coldStrings.size());
    entry.titleLength = static_cast<uint32_t>(meta->title.size());
    AppendBytes(coldStrings, meta->title.cstr(), meta->title.size());
    entry.descriptionOffset = coldStringBase + static_cast<uint32_t>(coldStrings.size());
    entry.descriptionLength = static_cast<uint32_t>(meta->description.size());
    AppendBytes(coldStrings, meta->description.cstr(), meta->description.size());
    coldDocEntries.pushBack(entry);
  }

  uint64_t offset = sizeof(ServingShardHeaderV1);
  header.lexiconOffset = offset;
  header.lexiconLength = lexicon.size() * sizeof(ServingLexiconEntryV1);
  offset += header.lexiconLength;
  header.termBytesOffset = offset;
  header.termBytesLength = termBytes.size();
  offset += header.termBytesLength;
  header.termInfoOffset = offset;
  header.termInfoLength = termInfos.size() * sizeof(ServingTermInfoV1);
  offset += header.termInfoLength;
  header.tierInfoOffset = offset;
  header.tierInfoLength = tierInfos.size() * sizeof(ServingTierInfoV1);
  offset += header.tierInfoLength;
  header.blockInfoOffset = offset;
  header.blockInfoLength = blockInfos.size() * sizeof(ServingBlockInfoV1);
  offset += header.blockInfoLength;
  header.postingsOffset = offset;
  header.postingsLength = postings.size();
  offset += header.postingsLength;
  header.docLengthOffset = offset;
  header.docLengthLength = shard.docLengths.size() * sizeof(uint32_t);
  offset += header.docLengthLength;
  header.staticRankOffset = offset;
  header.staticRankLength = 0;
  header.coldPayloadOffset = offset;
  header.coldPayloadLength =
      coldDocEntries.size() * sizeof(ServingDocMetadataEntryV1) + coldStrings.size();
  offset += header.coldPayloadLength;
  header.debugOffset = offset;
  header.debugLength = 0;

  out->clear();
  AppendRaw(*out, &header, 1);
  AppendRaw(*out, lexicon.begin(), lexicon.size());
  AppendBytes(*out, termBytes.begin(), termBytes.size());
  AppendRaw(*out, termInfos.begin(), termInfos.size());
  AppendRaw(*out, tierInfos.begin(), tierInfos.size());
  AppendRaw(*out, blockInfos.begin(), blockInfos.size());
  AppendBytes(*out, postings.begin(), postings.size());
  AppendRaw(*out, shard.docLengths.begin(), shard.docLengths.size());
  AppendRaw(*out, coldDocEntries.begin(), coldDocEntries.size());
  AppendBytes(*out, coldStrings.begin(), coldStrings.size());
  return true;
}

bool WriteServingShardBytes(const string& outputPath, const vector<uint8_t>& bytes,
                            string* error)
{
  const int fd = open(outputPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    SetErrnoError("open", outputPath, error);
    return false;
  }

  bool ok = WriteVector(fd, bytes);
  if (close(fd) != 0)
    ok = false;
  if (!ok)
  {
    SetErrnoError("write", outputPath, error);
    unlink(outputPath.cstr());
    return false;
  }
  return true;
}

bool LoadPrimaryShards(const vector<ServingShardFile>& primary,
                       vector<OutputShardState>* shards,
                       unordered_map<DocRef>* urlToDoc, string* error)
{
  if (!shards || !urlToDoc)
    return false;
  shards->clear();

  for (size_t i = 0; i < primary.size(); ++i)
  {
    OutputShardState state;
    state.file = &primary[i];
    state.reader = new LegacyShardReader(primary[i].inputPath);
    if (!state.reader->IsLoaded())
    {
      if (error)
      {
        *error = string("failed to read primary shard ");
        *error += primary[i].inputPath;
        *error += string(": ");
        *error += state.reader->GetError();
      }
      delete state.reader;
      return false;
    }

    state.docLengths.reserve(state.reader->DocumentCount());
    for (size_t doc = 0; doc < state.reader->DocumentCount(); ++doc)
    {
      const LegacyDocumentMetadata* meta = state.reader->GetDocument(static_cast<uint32_t>(doc));
      if (!meta)
      {
        if (error)
          *error = string("primary shard document metadata missing");
        delete state.reader;
        return false;
      }
      state.docLengths.pushBack(meta->docLength);
      urlToDoc->insert(meta->url, {i, meta->docId});
    }

    shards->pushBack(state);
  }

  return true;
}

bool AddPrimaryEvidence(OutputShardState* shard, string* error)
{
  if (!shard || !shard->reader)
    return false;

  for (size_t termIndex = 0; termIndex < shard->reader->TermCount(); ++termIndex)
  {
    const LegacyTermInfo* info = shard->reader->GetTerm(termIndex);
    if (!info || info->term == string(DOCEND_TOKEN))
      continue;

    const string rawTerm = info->term;
    if (rawTerm.size() == 0)
      continue;

    bool isTitle = false;
    bool ignore = false;
    string servingTerm = rawTerm;
    if (rawTerm[0] == '#')
    {
      isTitle = true;
      servingTerm = string();
      for (size_t i = 1; i < rawTerm.size(); ++i)
        servingTerm.pushBack(rawTerm[i]);
    }
    else if (rawTerm[0] == '/')
    {
      ignore = true;
    }
    if (ignore || servingTerm.size() == 0)
      continue;

    vector<LegacyDocTermStats> stats;
    if (!shard->reader->DecodeTermDocumentStats(info->term, &stats))
    {
      if (error)
      {
        *error = string("failed to decode term ");
        *error += info->term;
      }
      return false;
    }

    for (const LegacyDocTermStats& stat : stats)
    {
      if (isTitle)
        AddEvidence(shard->terms, servingTerm, stat.docId, 0u, stat.termFrequency, 0u);
      else
        AddEvidence(shard->terms, servingTerm, stat.docId, stat.termFrequency, 0u, 0u);
    }
  }

  return true;
}

bool AddAnchorEvidence(const vector<string>& anchors, vector<OutputShardState>* shards,
                       const unordered_map<DocRef>& urlToDoc, string* error)
{
  if (!shards)
    return false;

  for (const string& path : anchors)
  {
    LegacyShardReader anchor(path);
    if (!anchor.IsLoaded())
    {
      if (error)
      {
        *error = string("failed to read anchor shard ");
        *error += path;
        *error += string(": ");
        *error += anchor.GetError();
      }
      return false;
    }

    for (size_t termIndex = 0; termIndex < anchor.TermCount(); ++termIndex)
    {
      const LegacyTermInfo* info = anchor.GetTerm(termIndex);
      if (!info || info->term == string(DOCEND_TOKEN))
        continue;

      const string rawTerm = info->term;
      if (rawTerm.size() == 0 || rawTerm[0] == '#' || rawTerm[0] == '/')
        continue;

      vector<LegacyDocTermStats> stats;
      if (!anchor.DecodeTermDocumentStats(info->term, &stats))
      {
        if (error)
        {
          *error = string("failed to decode anchor term ");
          *error += info->term;
        }
        return false;
      }

      for (const LegacyDocTermStats& stat : stats)
      {
        const LegacyDocumentMetadata* meta = anchor.GetDocument(stat.docId);
        if (!meta)
          continue;
        const DocRef* found = urlToDoc.get(string_slice(meta->url.cstr()));
        if (!found)
          continue;
        OutputShardState& target = (*shards)[found->shardIndex];
        AddEvidence(target.terms, rawTerm, found->docId, 0u, 0u, stat.termFrequency);
      }
    }
  }

  return true;
}

} // namespace

bool DiscoverServingInputShards(const string& inputDirectory, vector<ServingShardFile>* primary,
                                vector<string>* anchors, string* error)
{
  if (!primary || !anchors)
    return false;

  primary->clear();
  anchors->clear();

  DIR* dir = opendir(inputDirectory.cstr());
  if (!dir)
  {
    SetErrnoError("opendir", inputDirectory, error);
    return false;
  }

  for (dirent* ent = readdir(dir); ent != nullptr; ent = readdir(dir))
  {
    uint64_t shardId = 0;
    if (IsPrimaryShardName(ent->d_name, &shardId))
    {
      const string name(ent->d_name);
      primary->pushBack({JoinPath(inputDirectory, name), ServingOutputName(shardId), shardId});
    }
    else if (IsAnchorShardName(ent->d_name))
    {
      anchors->pushBack(JoinPath(inputDirectory, string(ent->d_name)));
    }
  }

  closedir(dir);

  primary->sort([](const ServingShardFile& a, const ServingShardFile& b) {
    if (a.shardId != b.shardId)
      return a.shardId < b.shardId;
    return a.inputPath < b.inputPath;
  });
  anchors->sort([](const string& a, const string& b) {
    return a < b;
  });
  return true;
}

bool WriteEmptyServingShard(const string& outputPath, uint64_t shardId, uint32_t blockSize,
                            string* error)
{
  if (blockSize == 0)
  {
    if (error)
      *error = string("block size must be greater than zero");
    return false;
  }

  ServingShardHeaderV1 header = MakeEmptyServingShardHeaderV1(shardId);
  header.blockSize = blockSize;

  const int fd = open(outputPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    SetErrnoError("open", outputPath, error);
    return false;
  }

  bool ok = WriteAll(fd, &header, sizeof(header));
  if (close(fd) != 0)
    ok = false;

  if (!ok)
  {
    SetErrnoError("write", outputPath, error);
    unlink(outputPath.cstr());
    return false;
  }

  return true;
}

bool RebuildServingIndex(const ServingRebuildOptions& options, ServingRebuildStats* stats)
{
  ServingRebuildStats local = {};
  if (!stats)
    stats = &local;
  *stats = {};

  if (options.inputDirectory.size() == 0)
  {
    stats->error = string("missing input directory");
    return false;
  }
  if (options.outputDirectory.size() == 0)
  {
    stats->error = string("missing output directory");
    return false;
  }
  if (options.blockSize == 0)
  {
    stats->error = string("block size must be greater than zero");
    return false;
  }

  vector<ServingShardFile> primary;
  vector<string> anchors;
  if (!DiscoverServingInputShards(options.inputDirectory, &primary, &anchors, &stats->error))
    return false;

  stats->primaryShardCount = primary.size();
  stats->anchorShardCount = anchors.size();

  if (primary.empty())
  {
    stats->error = string("no primary NNNNN.sfi shards found in ");
    stats->error += options.inputDirectory;
    return false;
  }

  struct stat outStat = {};
  if (stat(options.outputDirectory.cstr(), &outStat) != 0)
  {
    SetErrnoError("stat", options.outputDirectory, &stats->error);
    return false;
  }
  if (!S_ISDIR(outStat.st_mode))
  {
    stats->error = string("output path is not a directory: ");
    stats->error += options.outputDirectory;
    return false;
  }

  vector<OutputShardState> shards;
  unordered_map<DocRef> urlToDoc;
  if (!LoadPrimaryShards(primary, &shards, &urlToDoc, &stats->error))
  {
    DestroyOutputShards(shards);
    return false;
  }

  for (OutputShardState& shard : shards)
  {
    if (!AddPrimaryEvidence(&shard, &stats->error))
    {
      DestroyOutputShards(shards);
      return false;
    }
  }

  if (!AddAnchorEvidence(anchors, &shards, urlToDoc, &stats->error))
  {
    DestroyOutputShards(shards);
    return false;
  }

  for (const OutputShardState& shard : shards)
  {
    vector<uint8_t> bytes;
    if (!BuildServingShardBytes(shard, options.blockSize, &bytes, &stats->error))
    {
      DestroyOutputShards(shards);
      return false;
    }

    const string outputPath = JoinPath(options.outputDirectory, shard.file->outputName);
    if (!WriteServingShardBytes(outputPath, bytes, &stats->error))
    {
      DestroyOutputShards(shards);
      return false;
    }
    ++stats->servingShardCount;
  }

  DestroyOutputShards(shards);
  return true;
}

bool RebuildServingIndexSkeleton(const ServingRebuildOptions& options, ServingRebuildStats* stats)
{
  ServingRebuildStats local = {};
  if (!stats)
    stats = &local;
  *stats = {};

  if (options.inputDirectory.size() == 0)
  {
    stats->error = string("missing input directory");
    return false;
  }
  if (options.outputDirectory.size() == 0)
  {
    stats->error = string("missing output directory");
    return false;
  }
  if (options.blockSize == 0)
  {
    stats->error = string("block size must be greater than zero");
    return false;
  }

  vector<ServingShardFile> primary;
  vector<string> anchors;
  if (!DiscoverServingInputShards(options.inputDirectory, &primary, &anchors, &stats->error))
    return false;

  stats->primaryShardCount = primary.size();
  stats->anchorShardCount = anchors.size();

  if (primary.empty())
  {
    stats->error = string("no primary NNNNN.sfi shards found in ");
    stats->error += options.inputDirectory;
    return false;
  }

  struct stat outStat = {};
  if (stat(options.outputDirectory.cstr(), &outStat) != 0)
  {
    SetErrnoError("stat", options.outputDirectory, &stats->error);
    return false;
  }
  if (!S_ISDIR(outStat.st_mode))
  {
    stats->error = string("output path is not a directory: ");
    stats->error += options.outputDirectory;
    return false;
  }

  for (size_t i = 0; i < primary.size(); ++i)
  {
    const string outputPath = JoinPath(options.outputDirectory, primary[i].outputName);
    if (!WriteEmptyServingShard(outputPath, primary[i].shardId, options.blockSize, &stats->error))
      return false;
    ++stats->servingShardCount;
  }

  return true;
}

} // namespace search
} // namespace avengine
