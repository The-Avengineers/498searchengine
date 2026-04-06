#include <avengine/search/search_service.h>

#include <avengine/ranker/ranker.h>
#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_common.h>

#include <chrono>
#include <ctime>
#include <dirent.h>

namespace avengine
{
namespace search
{
namespace
{

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

bool QueryExceededThreshold(const std::chrono::steady_clock::time_point& startedAt,
                            size_t thresholdMs)
{
  if (thresholdMs == 0)
    return true;

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startedAt);
  return static_cast<size_t>(elapsed.count()) > thresholdMs;
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

size_t ClockToMs(clock_t ticks)
{
  return static_cast<size_t>((static_cast<unsigned long>(ticks) * 1000u) / CLOCKS_PER_SEC);
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
  SearchResponse response = {};
  response.Query = query;
  response.Limit = ClampSearchLimit(limit);

  if (IsBlankText(query))
    return response;

  const auto startedAt = std::chrono::steady_clock::now();
  const size_t thresholdMs = solver_.GetExecutionTimeThresholdMs();
  const clock_t start = clock();
  constraint_solver::ParseResult parsed = solver_.ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    for (const auto& error : parsed.Errors)
      response.Errors.pushBack({error.Offset, error.Message});
    constraint_solver::DestroyQueryTree(parsed.Root);
    return response;
  }

  // Extract query terms from the AST for position collection
  vector<string> queryTerms;
  CollectQueryTerms(parsed.Root, queryTerms);
  const clock_t afterParse = clock();

  Ranker ranker;
  ranker.AddStrategy(&defaultStrategy_, 1.0);

  clock_t solveTotal = 0;
  clock_t collectTotal = 0;
  clock_t rankTotal = 0;

  for (const IndexShardSource* shard : shards_)
  {
    if (QueryExceededThreshold(startedAt, thresholdMs))
    {
      constraint_solver::DestroyQueryTree(parsed.Root);
      MarkTimeout(response, thresholdMs);
      return response;
    }

    const clock_t shardSolveStart = clock();
    const auto solved = solver_.SolveParsed(parsed.Root, *shard);
    if (solved.TimedOut)
    {
      constraint_solver::DestroyQueryTree(parsed.Root);
      MarkTimeout(response, thresholdMs);
      return response;
    }

    response.TotalHits += solved.Matches.size();
    solveTotal += clock() - shardSolveStart;

    if (solved.Matches.empty())
      continue;

    // Build per-match term position vectors by walking each term ISR once
    const clock_t shardCollectStart = clock();
    vector<MatchPositions> matchPositions;
    for (size_t i = 0; i < solved.Matches.size(); ++i)
    {
      MatchPositions mp;
      for (size_t t = 0; t < queryTerms.size(); ++t)
        mp.termPositions.pushBack(vector<size_t>());
      matchPositions.pushBack(mp);
    }

    // Build O(1) DocId → match-index lookup. DocIds are 0-based sequential
    // integers within a shard, so a flat vector indexed by DocId is cache-friendly.
    const uint32_t maxDocId = solved.Matches.back().DocId;
    vector<size_t> docIdToMatchIdx(static_cast<size_t>(maxDocId) + 1, solved.Matches.size());
    for (size_t i = 0; i < solved.Matches.size(); ++i)
      docIdToMatchIdx[solved.Matches[i].DocId] = i;

    for (size_t t = 0; t < queryTerms.size(); ++t)
    {
      if (QueryExceededThreshold(startedAt, thresholdMs))
      {
        constraint_solver::DestroyQueryTree(parsed.Root);
        MarkTimeout(response, thresholdMs);
        return response;
      }

      ISRWord* isr = shard->OpenTermIsr(queryTerms[t]);
      if (!isr)
        continue;

      for (Post* p = isr->Seek(1); p; p = isr->Next())
      {
        const Attributes attrs = p->GetAttributes();
        if (attrs.kind != Attributes::Kind::WORD)
          continue;

        const uint32_t docId = attrs.Word.documentId;
        if (docId > maxDocId)
          continue;

        const size_t idx = docIdToMatchIdx[docId];
        if (idx == solved.Matches.size())
          continue;

        matchPositions[idx].termPositions[t].pushBack(p->GetStartLocation());
      }
      delete isr;
    }
    collectTotal += clock() - shardCollectStart;

    // Score each match with real ISR-derived data
    const clock_t shardRankStart = clock();
    for (size_t i = 0; i < solved.Matches.size(); ++i)
    {
      if (QueryExceededThreshold(startedAt, thresholdMs))
      {
        constraint_solver::DestroyQueryTree(parsed.Root);
        MarkTimeout(response, thresholdMs);
        return response;
      }

      const auto& match = solved.Matches[i];
      const DocumentInfo* docInfo = shard->GetDocumentInfo(match.DocId);

      urlEntry entry = {};
      entry.url = match.Url;

      Ranker::RankSignals signals = {};
      if (docInfo)
        signals.title_word_count = docInfo->titleLength;

      ranker.Consider(entry, shard->GetDocumentTitle(match.DocId), matchPositions[i].termPositions,
                      signals);
    }
    rankTotal += clock() - shardRankStart;

    // Release cached posting list positions now that this shard's work is done.
    shard->ClearTermCache();
  }

  const auto& topResults = ranker.TopResults();
  for (size_t i = 0; i < topResults.size() && i < response.Limit; ++i)
    response.Results.pushBack({topResults[i].entry.url, topResults[i].title, topResults[i].score});

  constraint_solver::DestroyQueryTree(parsed.Root);
  const clock_t end = clock();
  response.TookMs = ClockToMs(end - start);
  response.ParseMs = ClockToMs(afterParse - start);
  response.SolveMs = ClockToMs(solveTotal);
  response.CollectMs = ClockToMs(collectTotal);
  response.RankMs = ClockToMs(rankTotal);
  return response;
}

} // namespace search
} // namespace avengine
