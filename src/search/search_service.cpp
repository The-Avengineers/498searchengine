#include <avengine/search/search_service.h>

#include <avengine/avengine_lib/thread.h>
#include <avengine/ranker/Strategy.h>
#include <avengine/ranker/ranker.h>
#include <avengine/search/coordinator_merge.h>
#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_common.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>
#include <vector>

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

struct CachedDecodedTerm
{
  vector<Location> positions;
  uint32_t documentFrequency = 0;
};

class QueryLocalShardSource : public constraint_solver::IsrSource
{
public:
  explicit QueryLocalShardSource(const IndexShardSource* shard) : shard_(shard) {}

  ISRWord* OpenTermIsr(const string& term) const override
  {
    const CachedDecodedTerm* cached = decodedTerms_.get(string_slice(term.cstr()));
    if (!cached)
    {
      CachedDecodedTerm decoded;
      if (!shard_ ||
          !shard_->DecodeTermOccurrences(term, &decoded.positions, &decoded.documentFrequency))
        return nullptr;

      decodedTerms_.insert(term, decoded);
      cached = decodedTerms_.get(string_slice(term.cstr()));
      if (!cached)
        return nullptr;
    }

    return shard_->BuildWordIsr(cached->positions, cached->documentFrequency);
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
  mutable unordered_map<CachedDecodedTerm> decodedTerms_;
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
};

struct PooledRankedHit
{
  Ranker::RankedResult ranked;
  size_t shardOrdinal = 0;
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

void CollectAllMatchPositions(ShardQueryContext& context, const vector<string>& terms)
{
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.bodyPositions);
  InitializeMatchPositions(context.solved.Matches.size(), terms.size(), context.titlePositions);

  CollectPositionsForTerms(context.querySource, context.solved.Matches, terms, string(),
                           context.bodyPositions);
  CollectPositionsForTerms(context.querySource, context.solved.Matches, terms, string("#"),
                           context.titlePositions);
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
  std::vector<ShardQueryContext>* contexts = nullptr;
  size_t begin = 0;
  size_t end = 0;
  SteadyTimePoint deadline = {};
  std::atomic<bool>* timedOut = nullptr;
};

void* RunSolvePhase(void* rawArgs)
{
  SolvePhaseArgs* args = static_cast<SolvePhaseArgs*>(rawArgs);
  if (!args || !args->solver || !args->contexts)
    return nullptr;

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if (QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(true);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[i];
    context.solved =
        args->solver->SolveParsedUntil(args->root, context.querySource, args->deadline);
    if (context.solved.TimedOut)
    {
      args->timedOut->store(true);
      return nullptr;
    }
  }

  return nullptr;
}

struct CollectPhaseArgs
{
  std::vector<ShardQueryContext>* contexts = nullptr;
  const std::vector<size_t>* matchedIndices = nullptr;
  const vector<string>* queryTerms = nullptr;
  size_t begin = 0;
  size_t end = 0;
  SteadyTimePoint deadline = {};
  std::atomic<bool>* timedOut = nullptr;
};

void* RunCollectPhase(void* rawArgs)
{
  CollectPhaseArgs* args = static_cast<CollectPhaseArgs*>(rawArgs);
  if (!args || !args->contexts || !args->matchedIndices || !args->queryTerms)
    return nullptr;

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if (QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(true);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[(*args->matchedIndices)[i]];
    CollectAllMatchPositions(context, *args->queryTerms);
  }

  return nullptr;
}

struct RankPhaseArgs
{
  std::vector<ShardQueryContext>* contexts = nullptr;
  const std::vector<size_t>* matchedIndices = nullptr;
  const vector<string>* queryTerms = nullptr;
  size_t begin = 0;
  size_t end = 0;
  size_t candidateLimit = 0;
  SteadyTimePoint deadline = {};
  std::atomic<bool>* timedOut = nullptr;
  std::vector<PooledRankedHit>* output = nullptr;
};

void* RunRankPhase(void* rawArgs)
{
  RankPhaseArgs* args = static_cast<RankPhaseArgs*>(rawArgs);
  if (!args || !args->contexts || !args->matchedIndices || !args->queryTerms || !args->output)
    return nullptr;

  Ranker ranker(args->candidateLimit);
  DomainStrat domainStrategy;
  ShortURLStrat shortUrlStrategy;
  ShortTitleStrat shortTitleStrategy;
  ExactPhraseMatch exactPhraseStrategy;
  CorrectOrdering correctOrderingStrategy;
  EarlyQueryTermMatch earlyQueryStrategy;
  TermCoverage termCoverageStrategy;
  UrlDepth urlDepthStrategy;
  QueryOccurrence queryOccurrenceStrategy(*args->queryTerms);

  ranker.AddStrategy(&exactPhraseStrategy, 2.4);
  ranker.AddStrategy(&correctOrderingStrategy, 1.6);
  ranker.AddStrategy(&earlyQueryStrategy, 1.25);
  ranker.AddStrategy(&termCoverageStrategy, 0.85);
  ranker.AddStrategy(&queryOccurrenceStrategy, 0.8);
  ranker.AddStrategy(&shortTitleStrategy, 0.5);
  ranker.AddStrategy(&shortUrlStrategy, 0.15);
  ranker.AddStrategy(&domainStrategy, 0.1);
  ranker.AddStrategy(&urlDepthStrategy, 0.15);

  for (size_t i = args->begin; i < args->end && !args->timedOut->load(); ++i)
  {
    if (QueryExceededDeadline(args->deadline))
    {
      args->timedOut->store(true);
      return nullptr;
    }

    ShardQueryContext& context = (*args->contexts)[(*args->matchedIndices)[i]];
    for (size_t matchIndex = 0; matchIndex < context.solved.Matches.size(); ++matchIndex)
    {
      if (QueryExceededDeadline(args->deadline))
      {
        args->timedOut->store(true);
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
        entry.depth = static_cast<int>(ranking_input::Depth(docInfo->rankingInput));
        signals.title_word_count = ranking_input::TitleWordCount(docInfo->rankingInput);
      }
      if (matchIndex < context.titlePositions.size())
        signals.title_term_positions = &context.titlePositions[matchIndex].termPositions;

      ranker.Consider(entry, context.shard->GetDocumentTitle(match.DocId),
                      context.bodyPositions[matchIndex].termPositions, signals,
                      context.shardOrdinal);
    }
  }

  const vector<Ranker::RankedResult> topResults = ranker.TopResults();
  args->output->clear();
  for (size_t i = 0; i < topResults.size(); ++i)
    args->output->push_back({topResults[i], topResults[i].shardOrdinal});

  return nullptr;
}

void LaunchSolvePhase(std::vector<ShardQueryContext>& contexts,
                      const constraint_solver::ConstraintSolver& solver,
                      const constraint_solver::QueryNode* root, size_t workerCount,
                      SteadyTimePoint deadline, std::atomic<bool>& timedOut)
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

void LaunchCollectPhase(std::vector<ShardQueryContext>& contexts,
                        const std::vector<size_t>& matchedIndices, const vector<string>& queryTerms,
                        size_t workerCount, SteadyTimePoint deadline, std::atomic<bool>& timedOut)
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

void LaunchRankPhase(std::vector<ShardQueryContext>& contexts,
                     const std::vector<size_t>& matchedIndices, const vector<string>& queryTerms,
                     size_t workerCount, size_t candidateLimit, SteadyTimePoint deadline,
                     std::atomic<bool>& timedOut,
                     std::vector<std::vector<PooledRankedHit>>& workerOutputs)
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
  };
  RunRankPhase(&current);

  for (size_t i = 0; i + 1 < phaseWorkers; ++i)
    workers[i].join();

  delete[] args;
  delete[] workers;
}

} // namespace

SearchService::SearchService(const string& indexDirectory) : indexDirectory_(indexDirectory)
{
  LoadShards();
}

SearchService::~SearchService()
{
  for (IndexShardSource* shard : shards_)
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
  for (const string& path : paths)
  {
    IndexShardSource* shard = new IndexShardSource(path);
    if (shard->IsLoaded())
      shards_.pushBack(shard);
    else
      delete shard;
  }
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
  response.Limit = limit == 0 ? 10 : limit;

  if (IsBlankText(query))
    return response;

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

  std::vector<ShardQueryContext> contexts;
  contexts.reserve(shards_.size());
  for (size_t i = 0; i < shards_.size(); ++i)
    contexts.emplace_back(shards_[i], i);

  const size_t shardWorkerCount = ResolveShardWorkerCount(shards_.size());
  std::atomic<bool> timedOut(false);

  const auto solveStartedAt = std::chrono::steady_clock::now();
  LaunchSolvePhase(contexts, solver_, parsed.Root, shardWorkerCount, deadline, timedOut);
  response.SolveMs = DurationToMs(std::chrono::steady_clock::now() - solveStartedAt);

  if (timedOut.load())
  {
    constraint_solver::DestroyQueryTree(parsed.Root);
    MarkTimeout(response, thresholdMs);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  std::vector<size_t> matchedIndices;
  matchedIndices.reserve(contexts.size());
  for (size_t i = 0; i < contexts.size(); ++i)
  {
    if (!contexts[i].solved.Matches.empty())
      matchedIndices.push_back(i);
    response.TotalHits += contexts[i].solved.Matches.size();
  }

  const auto collectStartedAt = std::chrono::steady_clock::now();
  LaunchCollectPhase(contexts, matchedIndices, queryTerms, shardWorkerCount, deadline, timedOut);
  response.CollectMs = DurationToMs(std::chrono::steady_clock::now() - collectStartedAt);

  if (timedOut.load())
  {
    constraint_solver::DestroyQueryTree(parsed.Root);
    MarkTimeout(response, thresholdMs);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  const auto rankStartedAt = std::chrono::steady_clock::now();
  std::vector<std::vector<PooledRankedHit>> workerOutputs;
  LaunchRankPhase(contexts, matchedIndices, queryTerms, shardWorkerCount, candidateLimit, deadline,
                  timedOut, workerOutputs);
  response.RankMs = DurationToMs(std::chrono::steady_clock::now() - rankStartedAt);

  if (timedOut.load())
  {
    constraint_solver::DestroyQueryTree(parsed.Root);
    MarkTimeout(response, thresholdMs);
    response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
    return response;
  }

  std::vector<PooledRankedHit> pooledHits;
  for (size_t workerIndex = 0; workerIndex < workerOutputs.size(); ++workerIndex)
  {
    for (size_t i = 0; i < workerOutputs[workerIndex].size(); ++i)
      pooledHits.push_back(workerOutputs[workerIndex][i]);
  }

  std::sort(pooledHits.begin(), pooledHits.end(),
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

  unordered_map<uint8_t> seenUrls;
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

    seenUrls.insert(url, 1u);
    if (uniqueIndex >= pageStart && uniqueIndex < pageEnd)
    {
      response.Results.pushBack(
          {pooledHits[i].ranked.entry.url, pooledHits[i].ranked.title, pooledHits[i].ranked.score});
    }
    ++uniqueIndex;
  }

  constraint_solver::DestroyQueryTree(parsed.Root);
  response.TookMs = DurationToMs(std::chrono::steady_clock::now() - startedAt);
  return response;
}

} // namespace search
} // namespace avengine
