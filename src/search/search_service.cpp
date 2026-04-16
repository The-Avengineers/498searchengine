#include <avengine/search/search_service.h>

#include <avengine/avengine_lib/thread.h>
#include <avengine/ranker/Strategy.h>
#include <avengine/ranker/ranker.h>
#include <avengine/search/coordinator_merge.h>
#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_common.h>

#include <avengine/avengine_lib/atomic.h>
#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>

namespace avengine
{
namespace search
{
namespace
{

using SteadyTimePoint = std::chrono::steady_clock::time_point;

bool HasSfiSuffix(const string& name)
{
  return name.size() >= 4 && name[name.size() - 4] == '.' && name[name.size() - 3] == 's' &&
         name[name.size() - 2] == 'f' && name[name.size() - 1] == 'i';
}

bool IsAnchorShard(const string& path)
{
  size_t nameStart = 0;
  for (size_t i = path.size(); i > 0; --i)
  {
    if (path[i - 1] == '/')
    {
      nameStart = i;
      break;
    }
  }
  const char marker[] = "anchor_";
  const size_t markerLen = 7;
  const size_t nameLen = path.size() - nameStart;
  if (nameLen < markerLen)
    return false;

  bool startsWithAnchor = true;
  for (size_t i = 0; i < markerLen; ++i)
  {
    if (path[nameStart + i] != marker[i])
    {
      startsWithAnchor = false;
      break;
    }
  }
  if (startsWithAnchor)
    return true;

  // Aggregated runtime shards can be prefixed by machine, e.g.
  // A_anchor_00511.sfi. Keep those out of the primary constraint solver too.
  for (size_t i = nameStart; i + markerLen < path.size(); ++i)
  {
    if (path[i] != '_')
      continue;

    bool hasAnchorMarker = true;
    for (size_t j = 0; j < markerLen; ++j)
    {
      if (path[i + 1 + j] != marker[j])
      {
        hasAnchorMarker = false;
        break;
      }
    }
    if (hasAnchorMarker)
      return true;
  }

  return false;
}

// Returns the registered domain from a URL.
// https://maps.google.com/path -> "google.com"

string ExtractRegisteredDomain(const string& url)
{
  // Find start of hostname after "://" and end of hostname at '/', '?', or ':'
  size_t host_start = 0;
  const size_t scheme_sep = url.find("://");
  if (scheme_sep != string::npos)
    host_start = scheme_sep + 3;

  size_t host_end = url.size();
  for (size_t i = host_start; i < url.size(); ++i)
  {
    if (url[i] == '/' || url[i] == '?' || url[i] == ':')
    {
      host_end = i;
      break;
    }
  }

  size_t last_dot = string::npos;
  for (size_t i = host_start; i < host_end; ++i)
    if (url[i] == '.')
      last_dot = i;

  if (last_dot == string::npos)
  {
    string result;
    for (size_t i = host_start; i < host_end; ++i)
      result.pushBack(url[i]);
    return result;
  }

  size_t second_last_dot = string::npos;
  for (size_t i = host_start; i < last_dot; ++i)
    if (url[i] == '.')
      second_last_dot = i;

  const size_t domain_start = second_last_dot != string::npos ? second_last_dot + 1 : host_start;
  string result;
  for (size_t i = domain_start; i < host_end; ++i)
    result.pushBack(url[i]);
  return result;
}

string JoinPath(const string& directory, const string& filename)
{
  string path = directory;
  if (path.size() != 0 && path[path.size() - 1] != '/')
    path.pushBack('/');
  path += filename;
  return path;
}

vector<string> DiscoverShardPaths(const string& directory)
{
  vector<string> paths;
  DIR* dir = opendir(directory.cstr());
  if (!dir)
    return paths;

  while (dirent* entry = readdir(dir))
  {
    if (entry->d_name[0] == '.')
      continue;

    const string name(entry->d_name);
    if (HasSfiSuffix(name))
      paths.pushBack(JoinPath(directory, name));
  }

  closedir(dir);
  // without sorting, the same query could return different top
  // results across runs just because shard enumeration changed
  paths.sort();
  return paths;
}

struct MatchPositions
{
  vector<vector<size_t>> termPositions; // termPositions[termIdx] = locations in document
};

class QueryLocalShardSource : public constraint_solver::IsrSource
{
public:
  explicit QueryLocalShardSource(const IndexShardSource* shard) : shard_(shard) {}

  ISRWord* OpenTermIsr(const string& term) const override
  {
    return shard_ ? shard_->OpenTermIsr(term) : nullptr;
  }

  ISREndDoc* OpenEndDocIsr() const override
  {
    return shard_ ? shard_->OpenEndDocIsr() : nullptr;
  }

  string GetDocumentURL(uint32_t docId) const override
  {
    return shard_ ? shard_->GetDocumentURL(docId) : string();
  }

private:
  const IndexShardSource* shard_ = nullptr;
};

struct ShardQueryContext
{
  explicit ShardQueryContext(const IndexShardSource* source = nullptr, size_t ordinal = 0)
      : shard(source), shardOrdinal(ordinal), querySource(source)
  {
  }

  const IndexShardSource* shard = nullptr;
  size_t shardOrdinal = 0;
  QueryLocalShardSource querySource;
  constraint_solver::SolveResult solved;
  vector<MatchPositions> bodyPositions;
  vector<MatchPositions> titlePositions;
  vector<MatchPositions> anchorPositions;
};

struct PooledRankedHit
{
  Ranker::RankedResult ranked;
  size_t shardOrdinal = 0;
  string description;
};

string ExtractDomain(const string& url)
{
  size_t hostStart = 0;
  const size_t schemeEnd = url.find("://");
  if (schemeEnd != string::npos)
    hostStart = schemeEnd + 3;

  size_t hostEnd = hostStart;
  while (hostEnd < url.size())
  {
    const char c = url[hostEnd];
    if (c == '/' || c == '?' || c == '#')
      break;
    ++hostEnd;
  }

  string domain;
  for (size_t i = hostStart; i < hostEnd; ++i)
    domain.pushBack(url[i]);
  return domain;
}

void InitializeMatchPositions(size_t matchCount, size_t termCount,
                              vector<MatchPositions>& matchPositions)
{
  matchPositions.clear();
  for (size_t i = 0; i < matchCount; ++i)
  {
    MatchPositions positions;
    for (size_t t = 0; t < termCount; ++t)
      positions.termPositions.pushBack(vector<size_t>());
    matchPositions.pushBack(positions);
  }
}

void CollectPositionsForTerms(const constraint_solver::IsrSource& source,
                              const vector<constraint_solver::DocumentMatch>& matches,
                              const vector<string>& terms, const string& prefix,
                              vector<MatchPositions>& matchPositions)
{
  for (size_t t = 0; t < terms.size(); ++t)
  {
    string lookup = prefix;
    lookup += terms[t];

    ISRWord* isr = source.OpenTermIsr(lookup);
    if (!isr)
      continue;

    size_t matchIdx = 0;
    for (Post* p = isr->Seek(1); p && matchIdx < matches.size(); p = isr->Next())
    {
      const Attributes attrs = p->GetAttributes();
      if (attrs.kind != Attributes::Kind::WORD)
        continue;

      const uint32_t docId = attrs.Word.documentId;
      while (matchIdx < matches.size() && matches[matchIdx].DocId < docId)
        ++matchIdx;

      if (matchIdx >= matches.size())
        break;

      if (matches[matchIdx].DocId == docId)
        matchPositions[matchIdx].termPositions[t].pushBack(p->GetStartLocation());
    }

    delete isr;
  }
}

void CollectQueryTerms(const constraint_solver::QueryNode* node, vector<string>& terms)
{
  if (!node)
    return;

  using constraint_solver::QueryNodeKind;
  switch (node->Kind)
  {
  case QueryNodeKind::Term:
    terms.pushBack(node->Value);
    break;
  case QueryNodeKind::Phrase:
    for (const string& t : node->PhraseTerms)
      terms.pushBack(t);
    break;
  case QueryNodeKind::Conjunction:
    for (const constraint_solver::QueryNode* child : node->RequiredChildren)
      CollectQueryTerms(child, terms);
    break;
  case QueryNodeKind::Or:
    for (const constraint_solver::QueryNode* child : node->Children)
      CollectQueryTerms(child, terms);
    break;
  default:
    break;
  }
}

unordered_map<MatchPositions> BuildAnchorPositionMap(
    const vector<IndexShardSource*>& anchorShards, const vector<string>& queryTerms)
{
  unordered_map<MatchPositions> anchorByUrl;

  for (size_t si = 0; si < anchorShards.size(); ++si)
  {
    const IndexShardSource* shard = anchorShards[si];
    if (!shard)
      continue;

    // Positions from different anchor shards use incompatible absolute coordinate
    // spaces, so each URL's positions must come from exactly one shard (the first
    // that claims it). Track which URLs were claimed by the current shard so later
    // shards can skip them without mixing coordinates.
    unordered_map<uint8_t> claimedThisShard;

    for (size_t t = 0; t < queryTerms.size(); ++t)
    {
      ISRWord* isr = shard->OpenTermIsr(queryTerms[t]);
      if (!isr)
        continue;

      uint32_t lastDocId = UINT32_MAX;
      MatchPositions* cachedMp = nullptr;

      for (Post* p = isr->Seek(1); p; p = isr->Next())
      {
        const Attributes attrs = p->GetAttributes();
        if (attrs.kind != Attributes::Kind::WORD)
          continue;

        const uint32_t docId = attrs.Word.documentId;
        if (docId != lastDocId)
        {
          lastDocId = docId;
          cachedMp = nullptr;

          const string url = shard->GetDocumentURL(docId);
          if (url.size() == 0)
            continue;

          MatchPositions* existing = anchorByUrl.get(string_slice(url.cstr()));
          if (existing)
          {
            // Only append to an entry claimed in the current shard — entries from
            // prior shards are in a different coordinate space.
            if (claimedThisShard.get(string_slice(url.cstr())))
              cachedMp = existing;
          }
          else
          {
            MatchPositions fresh;
            for (size_t ti = 0; ti < queryTerms.size(); ++ti)
              fresh.termPositions.pushBack(vector<size_t>());
            anchorByUrl.insert(url, fresh);
            cachedMp = anchorByUrl.get(string_slice(url.cstr()));
            claimedThisShard.insert(url, 1u);
          }
        }

        if (cachedMp)
          cachedMp->termPositions[t].pushBack(p->GetStartLocation());
      }

      delete isr;
    }
  }

  return anchorByUrl;
}

void CollectAllMatchPositions(ShardQueryContext& context, const vector<string>& terms,
                              const unordered_map<MatchPositions>* anchorByUrl)
{
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.bodyPositions);
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.titlePositions);
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.anchorPositions);

  CollectPositionsForTerms(context.querySource, context.solved.Matches, terms, string(),
                           context.bodyPositions);
  CollectPositionsForTerms(context.querySource, context.solved.Matches, terms, string("#"),
                           context.titlePositions);

  if (anchorByUrl)
  {
    for (size_t matchIdx = 0; matchIdx < context.solved.Matches.size(); ++matchIdx)
    {
      const string& url = context.solved.Matches[matchIdx].Url;
      const MatchPositions* ap = anchorByUrl->get(string_slice(url.cstr()));
      if (ap)
        context.anchorPositions[matchIdx] = *ap;
    }
  }
}

size_t DurationToMs(std::chrono::steady_clock::duration duration)
{
  return static_cast<size_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

bool QueryExceededDeadline(SteadyTimePoint deadline)
{
  return std::chrono::steady_clock::now() >= deadline;
}

string FindDescriptionForRankedHit(const vector<ShardQueryContext>& contexts,
                                   const vector<size_t>& matchedIndices,
                                   const Ranker::RankedResult& ranked)
{
  for (size_t i = 0; i < matchedIndices.size(); ++i)
  {
    const ShardQueryContext& context = contexts[matchedIndices[i]];
    if (context.shardOrdinal != ranked.shardOrdinal || context.shard == nullptr)
      continue;

    for (size_t matchIndex = 0; matchIndex < context.solved.Matches.size(); ++matchIndex)
    {
      const auto& match = context.solved.Matches[matchIndex];
      if (match.Url == ranked.entry.url)
        return context.shard->GetDocumentDescription(match.DocId);
    }
  }

  return string();
}

void MarkTimeout(SearchResponse& response, size_t thresholdMs)
{
  response.TotalHits = 0;
  response.Results.clear();
  response.Errors.clear();

  string message("query timed out after exceeding ");
  search::AppendUnsigned(message, thresholdMs);
  message += string(" ms threshold");
  response.Errors.pushBack({0, message});
}

size_t ComputeWindowStart(size_t page, size_t limit)
{
  if (page <= 1 || limit == 0)
  {
    return 0;
  }

  const size_t maxSize = static_cast<size_t>(-1);
  if (page - 1 > maxSize / limit)
  {
    return maxSize;
  }

  return (page - 1) * limit;
}

size_t NormalizeResultLimit(size_t limit)
{
  return limit == 0 ? kDefaultSearchLimit : limit;
}

size_t OnlineCpuCount()
{
  const long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
  return cpuCount > 0 ? static_cast<size_t>(cpuCount) : 1u;
}

size_t ParsePositiveSizeT(const char* text)
{
  if (!text || *text == '\0')
    return 0;

  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (!end || *end != '\0')
    return 0;
  return static_cast<size_t>(value);
}

size_t ResolveShardWorkerCount(size_t shardCount)
{
  if (shardCount <= 1)
    return shardCount == 0 ? 1u : shardCount;

  const char* configured = std::getenv("SEARCH_SHARD_WORKERS");
  const size_t requested = ParsePositiveSizeT(configured);
  if (requested > 0)
    return requested > shardCount ? shardCount : requested;

  size_t autoCount = OnlineCpuCount();
  if (autoCount > 8u)
    autoCount = 8u;
  if (autoCount == 0)
    autoCount = 1u;
  return autoCount > shardCount ? shardCount : autoCount;
}

size_t ClampWorkerCount(size_t desired, size_t itemCount)
{
  if (itemCount == 0)
    return 0;
  if (desired == 0)
    return 1;
  return desired > itemCount ? itemCount : desired;
}

size_t RangeBegin(size_t workerIndex, size_t workerCount, size_t itemCount)
{
  return (itemCount * workerIndex) / workerCount;
}

size_t RangeEnd(size_t workerIndex, size_t workerCount, size_t itemCount)
{
  return (itemCount * (workerIndex + 1u)) / workerCount;
}

struct SolvePhaseArgs
{
  const constraint_solver::ConstraintSolver* solver = nullptr;
  const constraint_solver::QueryNode* root = nullptr;
  vector<ShardQueryContext>* contexts = nullptr;
  size_t begin = 0;
  size_t end = 0;
  SteadyTimePoint deadline = {};
  atomic_counter* timedOut = nullptr;
};

void* RunSolvePhase(void* rawArgs)
{
  SolvePhaseArgs* args = static_cast<SolvePhaseArgs*>(rawArgs);
  if (!args || !args->solver || !args->contexts)
    return nullptr;

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if ((i & 63) == 0 && QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(1);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[i];
    context.solved =
        args->solver->SolveParsedUntil(args->root, context.querySource, args->deadline);
    if (context.solved.TimedOut)
    {
      args->timedOut->store(1);
    }
  }

  return nullptr;
}

struct CollectPhaseArgs
{
  vector<ShardQueryContext>* contexts = nullptr;
  const vector<size_t>* matchedIndices = nullptr;
  const vector<string>* queryTerms = nullptr;
  const unordered_map<MatchPositions>* anchorByUrl = nullptr;
  size_t begin = 0;
  size_t end = 0;
  SteadyTimePoint deadline = {};
  atomic_counter* timedOut = nullptr;
};

void* RunCollectPhase(void* rawArgs)
{
  CollectPhaseArgs* args = static_cast<CollectPhaseArgs*>(rawArgs);
  if (!args || !args->contexts || !args->matchedIndices || !args->queryTerms)
    return nullptr;

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if ((i & 63) == 0 && QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(1);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[(*args->matchedIndices)[i]];
    CollectAllMatchPositions(context, *args->queryTerms, args->anchorByUrl);
  }

  return nullptr;
}

struct RankPhaseArgs
{
  vector<ShardQueryContext>* contexts = nullptr;
  const vector<size_t>* matchedIndices = nullptr;
  const vector<string>* queryTerms = nullptr;
  size_t begin = 0;
  size_t end = 0;
  size_t candidateLimit = 0;
  SteadyTimePoint deadline = {};
  atomic_counter* timedOut = nullptr;
  vector<PooledRankedHit>* output = nullptr;
  const RankerWeights* weights = nullptr;
};

struct LoadShardArgs
{
  const vector<string>* paths = nullptr;
  size_t begin = 0;
  size_t end = 0;
  vector<IndexShardSource*> shards;
  vector<IndexShardSource*> anchorShards;
};

void* RunLoadShardRange(void* rawArgs)
{
  LoadShardArgs* args = static_cast<LoadShardArgs*>(rawArgs);
  if (!args || !args->paths)
    return nullptr;

  for (size_t i = args->begin; i < args->end; ++i)
  {
    const string& path = (*args->paths)[i];
    IndexShardSource* shard = new IndexShardSource(path);
    if (!shard->IsLoaded())
    {
      delete shard;
      continue;
    }

    if (IsAnchorShard(path))
      args->anchorShards.pushBack(shard);
    else
      args->shards.pushBack(shard);
  }

  return nullptr;
}

void* RunRankPhase(void* rawArgs)
{
  RankPhaseArgs* args = static_cast<RankPhaseArgs*>(rawArgs);
  if (!args || !args->contexts || !args->matchedIndices || !args->queryTerms || !args->output)
    return nullptr;

  const RankerWeights defaultWeights;
  const RankerWeights& w = args->weights ? *args->weights : defaultWeights;

  Ranker ranker(args->candidateLimit);
  ExactPhraseMatch exactPhraseStrategy;
  CorrectOrdering correctOrderingStrategy;
  EarlyQueryTermMatch earlyQueryStrategy;
  TermCoverage termCoverageStrategy;
  QueryOccurrence queryOccurrenceStrategy(*args->queryTerms);
  TitleTermCoverage titleTermCoverageStrategy;
  ExactTitleMatch exactTitleMatchStrategy(*args->queryTerms);
  SeedDomainDepth seedDomainDepthStrategy;
  ShortTitleStrat shortTitleStrategy;
  ShortURLStrat shortUrlStrategy;
  DomainStrat domainStrategy;
  UrlDepth urlDepthStrategy;
  UrlPathDepth urlPathDepthStrategy;
  QueryParamPenalty queryParamPenaltyStrategy;

  // Strategies are always registered in the ranker_weights.conf fixed order
  ranker.AddStrategy(&exactPhraseStrategy, w.exact_phrase);
  ranker.AddStrategy(&correctOrderingStrategy, w.correct_ordering);
  ranker.AddStrategy(&earlyQueryStrategy, w.early_query_term);
  ranker.AddStrategy(&termCoverageStrategy, w.term_coverage);
  ranker.AddStrategy(&queryOccurrenceStrategy, w.query_occurrence);
  ranker.AddStrategy(&shortTitleStrategy, w.short_title);
  ranker.AddStrategy(&shortUrlStrategy, w.short_url);
  ranker.AddStrategy(&domainStrategy, w.domain);
  ranker.AddStrategy(&urlDepthStrategy, w.url_depth);
  ranker.AddStrategy(&urlPathDepthStrategy, w.url_path_depth);
  ranker.AddStrategy(&titleTermCoverageStrategy, w.title_term_coverage);
  ranker.AddStrategy(&seedDomainDepthStrategy, w.seed_domain_depth);
  ranker.AddStrategy(&queryParamPenaltyStrategy, w.query_param_penalty);
  ranker.AddStrategy(&exactTitleMatchStrategy, w.exact_title_match);

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if ((i & 63) == 0 && QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(1);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[(*args->matchedIndices)[i]];
    for (size_t matchIndex = 0; matchIndex < context.solved.Matches.size(); ++matchIndex)
    {
      if ((matchIndex & 63) == 0 && QueryExceededDeadline(args->deadline))
      {
        args->timedOut->store(1);
        return nullptr;
      }

      const auto& match = context.solved.Matches[matchIndex];
      const DocumentInfo* docInfo = context.shard->GetDocumentInfo(match.DocId);

      urlEntry entry = {};
      entry.url = match.Url;
      entry.domain = ExtractDomain(match.Url);

      Ranker::RankSignals signals = {};
      if (docInfo)
      {
        entry.depth = static_cast<int>(docInfo->rankingInput.depth);
        entry.seedDomainDepth = static_cast<int>(docInfo->rankingInput.domainDepth);
        signals.title_word_count = docInfo->rankingInput.titleWordCount;
      }
      if (matchIndex < context.titlePositions.size())
        signals.title_term_positions = &context.titlePositions[matchIndex].termPositions;
      if (matchIndex < context.anchorPositions.size())
        signals.anchor_term_positions = &context.anchorPositions[matchIndex].termPositions;

      ranker.Consider(entry, context.shard->GetDocumentTitle(match.DocId),
                      context.bodyPositions[matchIndex].termPositions, signals,
                      context.shardOrdinal);
    }
  }

  const vector<Ranker::RankedResult> topResults = ranker.TopResults();
  args->output->clear();
  for (size_t i = 0; i < topResults.size(); ++i)
  {
    args->output->pushBack({topResults[i], topResults[i].shardOrdinal,
                            FindDescriptionForRankedHit(*args->contexts, *args->matchedIndices,
                                                        topResults[i])});
  }

  return nullptr;
}

void LaunchSolvePhase(vector<ShardQueryContext>& contexts,
                      const constraint_solver::ConstraintSolver& solver,
                      const constraint_solver::QueryNode* root, size_t workerCount,
                      SteadyTimePoint deadline, atomic_counter& timedOut)
{
  const size_t phaseWorkers = ClampWorkerCount(workerCount, contexts.size());
  if (phaseWorkers == 0)
    return;

  thread* workers = phaseWorkers > 1 ? new thread[phaseWorkers - 1] : nullptr;
  SolvePhaseArgs* args = phaseWorkers > 1 ? new SolvePhaseArgs[phaseWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < phaseWorkers; ++workerIndex)
  {
    args[workerIndex - 1] = {
        .solver = &solver,
        .root = root,
        .contexts = &contexts,
        .begin = RangeBegin(workerIndex, phaseWorkers, contexts.size()),
        .end = RangeEnd(workerIndex, phaseWorkers, contexts.size()),
        .deadline = deadline,
        .timedOut = &timedOut,
    };
    workers[workerIndex - 1].start(RunSolvePhase, &args[workerIndex - 1]);
  }

  SolvePhaseArgs current = {
      .solver = &solver,
      .root = root,
      .contexts = &contexts,
      .begin = 0,
      .end = RangeEnd(0, phaseWorkers, contexts.size()),
      .deadline = deadline,
      .timedOut = &timedOut,
  };
  RunSolvePhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

void LaunchCollectPhase(vector<ShardQueryContext>& contexts, const vector<size_t>& matchedIndices,
                        const vector<string>& queryTerms,
                        const unordered_map<MatchPositions>* anchorByUrl, size_t workerCount,
                        SteadyTimePoint deadline, atomic_counter& timedOut)
{
  const size_t phaseWorkers = ClampWorkerCount(workerCount, matchedIndices.size());
  if (phaseWorkers == 0)
    return;

  thread* workers = phaseWorkers > 1 ? new thread[phaseWorkers - 1] : nullptr;
  CollectPhaseArgs* args = phaseWorkers > 1 ? new CollectPhaseArgs[phaseWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < phaseWorkers; ++workerIndex)
  {
    args[workerIndex - 1] = {
        .contexts = &contexts,
        .matchedIndices = &matchedIndices,
        .queryTerms = &queryTerms,
        .anchorByUrl = anchorByUrl,
        .begin = RangeBegin(workerIndex, phaseWorkers, matchedIndices.size()),
        .end = RangeEnd(workerIndex, phaseWorkers, matchedIndices.size()),
        .deadline = deadline,
        .timedOut = &timedOut,
    };
    workers[workerIndex - 1].start(RunCollectPhase, &args[workerIndex - 1]);
  }

  CollectPhaseArgs current = {
      .contexts = &contexts,
      .matchedIndices = &matchedIndices,
      .queryTerms = &queryTerms,
      .anchorByUrl = anchorByUrl,
      .begin = 0,
      .end = RangeEnd(0, phaseWorkers, matchedIndices.size()),
      .deadline = deadline,
      .timedOut = &timedOut,
  };
  RunCollectPhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

void LaunchRankPhase(vector<ShardQueryContext>& contexts, const vector<size_t>& matchedIndices,
                     const vector<string>& queryTerms, size_t workerCount, size_t candidateLimit,
                     SteadyTimePoint deadline, atomic_counter& timedOut,
                     vector<vector<PooledRankedHit>>& workerOutputs, const RankerWeights& weights)
{
  const size_t phaseWorkers = ClampWorkerCount(workerCount, matchedIndices.size());
  workerOutputs.clear();
  workerOutputs.resize(phaseWorkers);
  if (phaseWorkers == 0)
    return;

  thread* workers = phaseWorkers > 1 ? new thread[phaseWorkers - 1] : nullptr;
  RankPhaseArgs* args = phaseWorkers > 1 ? new RankPhaseArgs[phaseWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < phaseWorkers; ++workerIndex)
  {
    args[workerIndex - 1] = {
        .contexts = &contexts,
        .matchedIndices = &matchedIndices,
        .queryTerms = &queryTerms,
        .begin = RangeBegin(workerIndex, phaseWorkers, matchedIndices.size()),
        .end = RangeEnd(workerIndex, phaseWorkers, matchedIndices.size()),
        .candidateLimit = candidateLimit,
        .deadline = deadline,
        .timedOut = &timedOut,
        .output = &workerOutputs[workerIndex],
        .weights = &weights,
    };
    workers[workerIndex - 1].start(RunRankPhase, &args[workerIndex - 1]);
  }

  RankPhaseArgs current = {
      .contexts = &contexts,
      .matchedIndices = &matchedIndices,
      .queryTerms = &queryTerms,
      .begin = 0,
      .end = RangeEnd(0, phaseWorkers, matchedIndices.size()),
      .candidateLimit = candidateLimit,
      .deadline = deadline,
      .timedOut = &timedOut,
      .output = &workerOutputs[0],
      .weights = &weights,
  };
  RunRankPhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

} // namespace

SearchService::SearchService(const string& indexDirectory, bool loadShards)
    : indexDirectory_(indexDirectory)
{
  if (loadShards)
    LoadShards();
}

SearchService::~SearchService()
{
  for (IndexShardSource* shard : shards_)
    delete shard;
  for (IndexShardSource* shard : anchorShards_)
    delete shard;
}

void SearchService::SetQueryExecutionTimeThresholdMs(size_t thresholdMs)
{
  solver_.SetExecutionTimeThresholdMs(thresholdMs);
}

size_t SearchService::GetQueryExecutionTimeThresholdMs() const
{
  return solver_.GetExecutionTimeThresholdMs();
}

void SearchService::LoadShards()
{
  const vector<string> paths = DiscoverShardPaths(indexDirectory_);
  const size_t loadWorkers = ResolveShardWorkerCount(paths.size());
  if (loadWorkers == 0)
    return;

  LoadShardArgs* args = new LoadShardArgs[loadWorkers];
  thread* workers = loadWorkers > 1 ? new thread[loadWorkers - 1] : nullptr;

  for (size_t workerIndex = 1; workerIndex < loadWorkers; ++workerIndex)
  {
    args[workerIndex].paths = &paths;
    args[workerIndex].begin = RangeBegin(workerIndex, loadWorkers, paths.size());
    args[workerIndex].end = RangeEnd(workerIndex, loadWorkers, paths.size());
    if (!workers[workerIndex - 1].start(RunLoadShardRange, &args[workerIndex]))
      RunLoadShardRange(&args[workerIndex]);
  }

  args[0].paths = &paths;
  args[0].begin = 0;
  args[0].end = RangeEnd(0, loadWorkers, paths.size());
  RunLoadShardRange(&args[0]);

  for (size_t i = 0; i + 1 < loadWorkers; ++i)
  {
    if (workers[i].joinable())
      workers[i].join();
  }

  for (size_t workerIndex = 0; workerIndex < loadWorkers; ++workerIndex)
  {
    for (size_t i = 0; i < args[workerIndex].shards.size(); ++i)
      shards_.pushBack(args[workerIndex].shards[i]);
    for (size_t i = 0; i < args[workerIndex].anchorShards.size(); ++i)
      anchorShards_.pushBack(args[workerIndex].anchorShards[i]);
  }

  delete[] workers;
  delete[] args;
}

SearchResponse SearchService::Search(const string& query, size_t limit) const
{
  return SearchPage(query, 1, limit);
}

SearchResponse SearchService::SearchPage(const string& query, size_t page, size_t limit) const
{
  SearchResponse response = {};
  response.Query = query;
  response.Page = page == 0 ? 1 : page;
  response.Limit = NormalizeResultLimit(limit);

  if (IsBlankText(query))
    return response;

  const RankerWeights weights = LoadRankerWeights("ranker_weights.conf");

  const auto startedAt = std::chrono::steady_clock::now();
  const size_t thresholdMs = solver_.GetExecutionTimeThresholdMs();
  const SteadyTimePoint deadline = startedAt + std::chrono::milliseconds(thresholdMs);
  const size_t candidateLimit = ComputeOverfetchLimit(response.Page, response.Limit);

  const auto parseStartedAt = std::chrono::steady_clock::now();
  constraint_solver::ParseResult parsed = solver_.ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    for (const auto& error : parsed.Errors)
      response.Errors.pushBack({error.Offset, error.Message});
    constraint_solver::DestroyQueryTree(parsed.Root);
    response.ParseMs = DurationToMs(std::chrono::steady_clock::now() - parseStartedAt);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  vector<string> queryTerms;
  CollectQueryTerms(parsed.Root, queryTerms);
  response.ParseMs = DurationToMs(std::chrono::steady_clock::now() - parseStartedAt);

  if (QueryExceededDeadline(deadline))
  {
    constraint_solver::DestroyQueryTree(parsed.Root);
    MarkTimeout(response, thresholdMs);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  vector<ShardQueryContext> contexts;
  contexts.reserve(shards_.size());
  for (size_t i = 0; i < shards_.size(); ++i)
    contexts.emplaceBack(shards_[i], i);

  const size_t shardWorkerCount = ResolveShardWorkerCount(shards_.size());
  atomic_counter timedOut(0);

  const auto solveStartedAt = std::chrono::steady_clock::now();
  LaunchSolvePhase(contexts, solver_, parsed.Root, shardWorkerCount, deadline, timedOut);
  response.SolveMs = DurationToMs(std::chrono::steady_clock::now() - solveStartedAt);

  const bool solveTimedOut = timedOut.load();
  timedOut.store(0);

  // If solve timed out give rest of pipelineive a
  // new window so they can finish processing the partial results
  const SteadyTimePoint postSolveDeadline =
      solveTimedOut
          ? std::chrono::steady_clock::now() + std::chrono::milliseconds(thresholdMs)
          : deadline;

  vector<size_t> matchedIndices;
  matchedIndices.reserve(contexts.size());
  for (size_t i = 0; i < contexts.size(); ++i)
  {
    if (!contexts[i].solved.Matches.empty())
      matchedIndices.pushBack(i);
    response.TotalHits += contexts[i].solved.Matches.size();
  }

  const unordered_map<MatchPositions> anchorByUrl =
      (!matchedIndices.empty() && !anchorShards_.empty())
          ? BuildAnchorPositionMap(anchorShards_, queryTerms)
          : unordered_map<MatchPositions>();

  const auto collectStartedAt = std::chrono::steady_clock::now();
  LaunchCollectPhase(contexts, matchedIndices, queryTerms, &anchorByUrl, shardWorkerCount,
                     postSolveDeadline, timedOut);
  response.CollectMs = DurationToMs(std::chrono::steady_clock::now() - collectStartedAt);

  const bool collectTimedOut = timedOut.load();
  timedOut.store(0);

  const SteadyTimePoint postCollectDeadline =
      collectTimedOut
          ? std::chrono::steady_clock::now() + std::chrono::milliseconds(thresholdMs)
          : postSolveDeadline;

  const auto rankStartedAt = std::chrono::steady_clock::now();
  vector<vector<PooledRankedHit>> workerOutputs;
  LaunchRankPhase(contexts, matchedIndices, queryTerms, shardWorkerCount, candidateLimit,
                  postCollectDeadline, timedOut, workerOutputs, weights);
  response.RankMs = DurationToMs(std::chrono::steady_clock::now() - rankStartedAt);

  vector<PooledRankedHit> pooledHits;
  for (size_t workerIndex = 0; workerIndex < workerOutputs.size(); ++workerIndex)
  {
    for (size_t i = 0; i < workerOutputs[workerIndex].size(); ++i)
      pooledHits.pushBack(workerOutputs[workerIndex][i]);
  }

  pooledHits.sort(
      [](const PooledRankedHit& lhs, const PooledRankedHit& rhs)
      {
        if (lhs.ranked.score != rhs.ranked.score)
          return lhs.ranked.score > rhs.ranked.score;
        if (lhs.shardOrdinal != rhs.shardOrdinal)
          return lhs.shardOrdinal < rhs.shardOrdinal;
        if (lhs.ranked.insertionIndex != rhs.ranked.insertionIndex)
          return lhs.ranked.insertionIndex < rhs.ranked.insertionIndex;
        return lhs.ranked.entry.url < rhs.ranked.entry.url;
      });

  static constexpr uint8_t kMaxResultsPerDomain = 5;

  unordered_map<uint8_t> seenUrls;
  unordered_map<uint8_t> domainCounts;
  const size_t pageStart = ComputeWindowStart(response.Page, response.Limit);
  const size_t pageEnd = pageStart > static_cast<size_t>(-1) - response.Limit
                             ? static_cast<size_t>(-1)
                             : pageStart + response.Limit;

  size_t uniqueIndex = 0;
  for (size_t i = 0; i < pooledHits.size(); ++i)
  {
    const string& url = pooledHits[i].ranked.entry.url;
    const uint8_t* seen = seenUrls.get(string_slice(url.cstr()));
    if (seen && *seen != 0)
      continue;

    const string domain = ExtractRegisteredDomain(url);
    const uint8_t* dc = domainCounts.get(string_slice(domain.cstr()));
    const uint8_t count = dc ? *dc : 0;
    if (count >= kMaxResultsPerDomain)
      continue;

    seenUrls.insert(url, 1u);
    domainCounts.insert(domain, count + 1);
    if (uniqueIndex >= pageStart && uniqueIndex < pageEnd)
    {
      SearchHit hit;
      hit.Url = pooledHits[i].ranked.entry.url;
      hit.Title = pooledHits[i].ranked.title;
      hit.Description = pooledHits[i].description;
      hit.Score = pooledHits[i].ranked.score;
      response.Results.pushBack(hit);
    }
    ++uniqueIndex;
    if (uniqueIndex >= kMaxVisibleSearchResults)
      break;
  }
  if (response.TotalHits > kMaxVisibleSearchResults)
    response.TotalHits = kMaxVisibleSearchResults;

  {
    // Rewrite score.log from scratch on each query
    FILE* log = fopen("score.log", "w");
    if (log != nullptr)
    {
      static const char* kNames[] = {
          "exact_phrase",       "correct_ordering", "early_query_term",   "term_coverage",
          "query_occurrence",   "short_title",      "short_url",          "domain",
          "url_depth",          "url_path_depth",   "title_term_coverage", "seed_domain_depth",
          "query_param_penalty",  "exact_title_match",
      };
      static const size_t kNamesLen = sizeof(kNames) / sizeof(kNames[0]);

      fprintf(log, "query: %s\n", query.cstr());
      fprintf(log, "timing: parse=%zums solve=%zums collect=%zums rank=%zums\n",
              response.ParseMs, response.SolveMs, response.CollectMs, response.RankMs);
      fprintf(log, "terms:");
      for (size_t t = 0; t < queryTerms.size(); ++t)
        fprintf(log, " [%s]", queryTerms[t].cstr());
      fprintf(log, "\n");
      unordered_map<uint8_t> logSeen;
      size_t rank = 0;
      for (size_t i = 0; i < pooledHits.size() && rank < kMaxVisibleSearchResults; ++i)
      {
        const string& url = pooledHits[i].ranked.entry.url;
        const uint8_t* seen = logSeen.get(string_slice(url.cstr()));
        if (seen && *seen != 0)
          continue;
        logSeen.insert(url, 1u);
        fprintf(log, "  #%zu total=%.4f %s\n", rank + 1, pooledHits[i].ranked.score, url.cstr());
        fprintf(log, "        title: %s\n", pooledHits[i].ranked.title.cstr());
        const vector<double>& ss = pooledHits[i].ranked.strategy_scores;
        for (size_t s = 0; s < ss.size() && s < kNamesLen; ++s)
          fprintf(log, "        %-22s %.4f\n", kNames[s], ss[s]);
        ++rank;
      }
      fclose(log);
    }
  }

  constraint_solver::DestroyQueryTree(parsed.Root);
  response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
  return response;
}

} // namespace search
} // namespace avengine
