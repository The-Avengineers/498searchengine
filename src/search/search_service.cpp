#include <avengine/search/search_service.h>

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
  std::sort(paths.begin(), paths.end());
  return paths;
}

} // namespace

SearchService::SearchService(const string& indexDirectory) : indexDirectory_(indexDirectory) {}

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

  const vector<string> shardPaths = DiscoverShardPaths(indexDirectory_);
  for (const string& shardPath : shardPaths)
  {
    IndexShardSource shard(shardPath);
    if (!shard.IsLoaded())
      continue;

    const auto solved = solver_.SolveParsed(parsed.Root, shard);
    response.TotalHits += solved.Matches.size();
    for (size_t i = 0; i < solved.Matches.size() && response.Results.size() < response.Limit; ++i)
    {
      const auto& match = solved.Matches[i];
      // TODO: This is the deliberate stub seam for the next batch. Right now solver decides the
      // set, and every surviving page ties until the ranker gets wired in.
      response.Results.pushBack({match.Url, shard.GetDocumentTitle(match.DocId), 1.0});
    }
  }

  constraint_solver::DestroyQueryTree(parsed.Root);
  response.TookMs = static_cast<size_t>(((clock() - start) * 1000) / CLOCKS_PER_SEC);
  return response;
}

} // namespace search
} // namespace avengine
