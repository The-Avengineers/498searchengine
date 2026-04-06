#include <avengine/search/search_service.h>

#include <avengine/ranker/Strategy.h>
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

void CollectPositionsForTerms(const IndexShardSource* shard,
                              const vector<constraint_solver::DocumentMatch>& matches,
                              const vector<string>& terms, const string& prefix,
                              vector<MatchPositions>& matchPositions)
{
  for (size_t t = 0; t < terms.size(); ++t)
  {
    string lookup = prefix;
    lookup += terms[t];

    ISRWord* isr = shard->OpenTermIsr(lookup);
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

bool QueryExceededThreshold(const std::chrono::steady_clock::time_point& startedAt,
                            size_t thresholdMs)
{
  if (thresholdMs == 0)
    return false;

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

size_t ComputeCandidateLimit(size_t page, size_t limit)
{
  if (page == 0 || limit == 0)
  {
    return limit == 0 ? 10u : limit;
  }

  if (page > (static_cast<size_t>(-1) / limit))
  {
    return static_cast<size_t>(-1);
  }

  const size_t candidateLimit = page * limit;
  return candidateLimit == 0 ? limit : candidateLimit;
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

  const size_t candidateLimit = ComputeCandidateLimit(response.Page, response.Limit);
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

  Ranker ranker(candidateLimit);
  DomainStrat domainStrategy;
  ShortURLStrat shortUrlStrategy;
  ShortTitleStrat shortTitleStrategy;
  ExactPhraseMatch exactPhraseStrategy;
  CorrectOrdering correctOrderingStrategy;
  EarlyQueryTermMatch earlyQueryStrategy;
  TermCoverage termCoverageStrategy;
  UrlDepth urlDepthStrategy;
  QueryOccurrence queryOccurrenceStrategy(queryTerms);

  ranker.AddStrategy(&exactPhraseStrategy, 2.4);
  ranker.AddStrategy(&correctOrderingStrategy, 1.6);
  ranker.AddStrategy(&earlyQueryStrategy, 1.25);
  ranker.AddStrategy(&termCoverageStrategy, 0.85);
  ranker.AddStrategy(&queryOccurrenceStrategy, 0.8);
  ranker.AddStrategy(&shortTitleStrategy, 0.5);
  ranker.AddStrategy(&shortUrlStrategy, 0.15);
  ranker.AddStrategy(&domainStrategy, 0.1);
  ranker.AddStrategy(&urlDepthStrategy, 0.15);

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

    vector<MatchPositions> bodyPositions;
    vector<MatchPositions> titlePositions;
    const clock_t shardCollectStart = clock();
    for (size_t i = 0; i < solved.Matches.size(); ++i)
    {
      MatchPositions body;
      MatchPositions title;
      for (size_t t = 0; t < queryTerms.size(); ++t)
      {
        body.termPositions.pushBack(vector<size_t>());
        title.termPositions.pushBack(vector<size_t>());
      }
      bodyPositions.pushBack(body);
      titlePositions.pushBack(title);
    }

    CollectPositionsForTerms(shard, solved.Matches, queryTerms, string(), bodyPositions);
    CollectPositionsForTerms(shard, solved.Matches, queryTerms, string("#"), titlePositions);
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
      entry.domain = ExtractDomain(match.Url);

      Ranker::RankSignals signals = {};
      if (docInfo)
      {
        entry.depth = static_cast<int>(ranking_input::Depth(docInfo->rankingInput));
        signals.title_word_count = ranking_input::TitleWordCount(docInfo->rankingInput);
      }
      if (i < titlePositions.size())
        signals.title_term_positions = &titlePositions[i].termPositions;

      ranker.Consider(entry, shard->GetDocumentTitle(match.DocId), bodyPositions[i].termPositions,
                      signals);
    }
    rankTotal += clock() - shardRankStart;

    // Release cached posting list positions now that this shard's work is done.
    shard->ClearTermCache();
  }

  const auto& topResults = ranker.TopResults();
  const size_t pageStart = ComputeWindowStart(response.Page, response.Limit);
  const size_t pageEnd = pageStart > static_cast<size_t>(-1) - response.Limit
                             ? static_cast<size_t>(-1)
                             : pageStart + response.Limit;
  for (size_t i = pageStart; i < topResults.size() && i < pageEnd; ++i)
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
