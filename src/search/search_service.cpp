#include <avengine/search/search_service.h>

#include <avengine/ranker/ranker.h>
#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_common.h>

#include <algorithm>
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
  std::sort(paths.begin(), paths.end());
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

  Ranker ranker;
  ranker.AddStrategy(&defaultStrategy_, 1.0);

  for (const IndexShardSource* shard : shards_)
  {
    const auto solved = solver_.SolveParsed(parsed.Root, *shard);
    response.TotalHits += solved.Matches.size();

    if (solved.Matches.empty())
      continue;

    // Build per-match term position vectors by walking each term ISR once
    vector<MatchPositions> matchPositions;
    for (size_t i = 0; i < solved.Matches.size(); ++i)
    {
      MatchPositions mp;
      for (size_t t = 0; t < queryTerms.size(); ++t)
        mp.termPositions.pushBack(vector<size_t>());
      matchPositions.pushBack(mp);
    }

    for (size_t t = 0; t < queryTerms.size(); ++t)
    {
      ISRWord* isr = shard->OpenTermIsr(queryTerms[t]);
      if (!isr)
        continue;

      size_t matchIdx = 0;
      for (Post* p = isr->Seek(1); p && matchIdx < solved.Matches.size(); p = isr->Next())
      {
        const Attributes attrs = p->GetAttributes();
        if (attrs.kind != Attributes::Kind::WORD)
          continue;

        const uint32_t docId = attrs.Word.documentId;

        // Advance past matches with smaller docIds
        while (matchIdx < solved.Matches.size() && solved.Matches[matchIdx].DocId < docId)
          ++matchIdx;

        if (matchIdx >= solved.Matches.size())
          break;

        if (solved.Matches[matchIdx].DocId == docId)
          matchPositions[matchIdx].termPositions[t].pushBack(p->GetStartLocation());
      }
      delete isr;
    }

    // Score each match with real ISR-derived data
    for (size_t i = 0; i < solved.Matches.size(); ++i)
    {
      const auto& match = solved.Matches[i];
      const DocumentInfo* docInfo = shard->GetDocumentInfo(match.DocId);

      urlEntry entry = {};
      entry.url = match.Url;

      Ranker::RankSignals signals = {};
      if (docInfo)
        signals.title_word_count = docInfo->titleLength;

      ranker.Consider(entry, shard->GetDocumentTitle(match.DocId),
                      matchPositions[i].termPositions, signals);
    }
  }

  const auto& topResults = ranker.TopResults();
  for (size_t i = 0; i < topResults.size() && i < response.Limit; ++i)
    response.Results.pushBack(
        {topResults[i].entry.url, topResults[i].title, topResults[i].score});

  constraint_solver::DestroyQueryTree(parsed.Root);
  response.TookMs = static_cast<size_t>(((clock() - start) * 1000) / CLOCKS_PER_SEC);
  return response;
}

} // namespace search
} // namespace avengine
