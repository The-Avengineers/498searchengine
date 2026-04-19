#include <avengine/indexer/indexer.h>

#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_service.h>
#include <avengine/search/serving_index_rebuilder.h>

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
using avengine::string;
using avengine::vector;
using avengine::search::SearchResponse;
using avengine::search::SearchService;

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "search_service_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

struct TestDocument
{
  const char* Url;
  const char* Title;
  const char* Body;
  const char* Description = "";
};

void ingestBody(::Index& index, const char* body)
{
  string token;
  for (size_t i = 0;; ++i)
  {
    const char c = body[i];
    if (c == ' ' || c == '\0')
    {
      if (token.size() != 0)
      {
        index.ingest(token.cstr());
        token = string();
      }
      if (c == '\0')
        return;
      continue;
    }

    token.pushBack(c);
  }
}

uint16_t countWords(const string& text)
{
  uint16_t count = 0;
  bool inWord = false;
  for (size_t i = 0; i < text.size(); ++i)
  {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (std::isalpha(c))
    {
      if (!inWord)
      {
        ++count;
        inWord = true;
      }
    }
    else
    {
      inWord = false;
    }
  }
  return count;
}

void writeShard(const char* path, std::initializer_list<TestDocument> docs)
{
  ::Index index;
  for (const TestDocument& doc : docs)
  {
    string url(doc.Url);
    string title(doc.Title);
    string description(doc.Description ? doc.Description : "");
    index.beginDocument({.url = url,
                         .title = title,
                         .description = description,
                         .input = {.domainDepth = 0u,
                                   .depth = 0u,
                                   .urlLength = static_cast<uint16_t>(url.size()),
                                   .titleWordCount = countWords(title),
                                   .seedSource = 0u}});
    ingestBody(index, doc.Body);
    index.endDocument();
  }

  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  EXPECT(fd >= 0);
  EXPECT(index.serialize(fd) == false);
  EXPECT(close(fd) == 0);
}

string joinPath(const char* directory, const char* filename)
{
  string path(directory);
  path.pushBack('/');
  path += string(filename);
  return path;
}

string cachePathForShard(const string& shardPath)
{
  string path(shardPath);
  path += string(".loadcache");
  return path;
}

void writeBytes(const string& path, const char* bytes, size_t len)
{
  const int fd = open(path.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  EXPECT(fd >= 0);
  EXPECT(write(fd, bytes, len) == static_cast<ssize_t>(len));
  EXPECT(close(fd) == 0);
}

void removeShardAndCache(const string& shardPath)
{
  const string cachePath = cachePathForShard(shardPath);
  if (unlink(cachePath.cstr()) != 0)
    EXPECT(errno == ENOENT);
  EXPECT(unlink(shardPath.cstr()) == 0);
}

void expectUrls(const SearchResponse& response, std::initializer_list<const char*> urls)
{
  EXPECT(response.Results.size() == urls.size());
  size_t i = 0;
  for (const char* url : urls)
  {
    EXPECT(response.Results[i].Url == string(url));
    ++i;
  }
}

bool nearlyEqual(double a, double b)
{
  return std::fabs(a - b) < 1e-9;
}

void testRankingInputRoundTrip()
{
  char dirTemplate[] = "/tmp/search_service_round_trip_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  writeShard(shardPath.cstr(), {{"doc://static", "Static Title", "static body"}});

  avengine::search::IndexShardSource shard(shardPath);
  EXPECT(shard.IsLoaded());
  const avengine::DocumentInfo* info = shard.GetDocumentInfo(0);
  EXPECT(info != nullptr);

  const RankingInput expected = {.domainDepth = 0u,
                                 .depth = 0u,
                                 .urlLength = static_cast<uint16_t>(string("doc://static").size()),
                                 .titleWordCount = 2u,
                                 .seedSource = 0u};
  EXPECT(info->rankingInput.domainDepth == expected.domainDepth);
  EXPECT(info->rankingInput.depth == expected.depth);
  EXPECT(info->rankingInput.urlLength == expected.urlLength);
  EXPECT(info->rankingInput.titleWordCount == expected.titleWordCount);
  EXPECT(info->rankingInput.seedSource == expected.seedSource);

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testShardLoadCacheCreatedAndReused()
{
  char dirTemplate[] = "/tmp/search_service_load_cache_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  const string cachePath = cachePathForShard(shardPath);
  writeShard(shardPath.cstr(), {{"doc://cache-a", "Cache A", "alpha beta"}});

  EXPECT(access(cachePath.cstr(), F_OK) != 0);

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Rebuilt);
  }
  EXPECT(access(cachePath.cstr(), F_OK) == 0);

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Hit);
    EXPECT(shard.GetDocumentURL(0) == string("doc://cache-a"));
    avengine::ISRWord* isr = shard.OpenTermIsr(string("alpha"));
    EXPECT(isr != nullptr);
    delete isr;
  }

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testShardLoadCacheCorruptionFallsBackAndRewrites()
{
  char dirTemplate[] = "/tmp/search_service_bad_cache_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  const string cachePath = cachePathForShard(shardPath);
  writeShard(shardPath.cstr(), {{"doc://cache-b", "Cache B", "gamma delta"}});

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Rebuilt);
  }

  writeBytes(cachePath, "bad", 3);

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Rebuilt);
    EXPECT(shard.GetDocumentTitle(0) == string("Cache B"));
  }

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Hit);
  }

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testShardLoadCacheStaleSourceFallsBackAndRewrites()
{
  char dirTemplate[] = "/tmp/search_service_stale_cache_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  const string cachePath = cachePathForShard(shardPath);
  writeShard(shardPath.cstr(), {{"doc://old", "Old", "alpha"}});

  struct stat before = {};
  EXPECT(stat(shardPath.cstr(), &before) == 0);

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Rebuilt);
  }

  writeShard(shardPath.cstr(),
             {{"doc://new", "New", "beta beta beta beta beta beta beta beta beta beta"}});

  struct stat after = {};
  EXPECT(stat(shardPath.cstr(), &after) == 0);
  EXPECT(before.st_size != after.st_size);

  {
    avengine::search::IndexShardSource shard(shardPath);
    EXPECT(shard.IsLoaded());
    EXPECT(shard.GetCacheStatus() == avengine::search::ShardLoadCacheStatus::Rebuilt);
    EXPECT(shard.GetDocumentURL(0) == string("doc://new"));
    avengine::ISRWord* oldIsr = shard.OpenTermIsr(string("alpha"));
    EXPECT(oldIsr == nullptr);
    avengine::ISRWord* newIsr = shard.OpenTermIsr(string("beta"));
    EXPECT(newIsr != nullptr);
    delete newIsr;
  }

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testPrefixedShardNamesAreDiscoveredAndClassified()
{
  char dirTemplate[] = "/tmp/search_service_prefixed_names_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string primaryA = joinPath(dir, "A_00485.sfi");
  const string primaryB = joinPath(dir, "B_00750.sfi");
  const string anchorA = joinPath(dir, "A_anchor_00055.sfi");
  const string anchorB = joinPath(dir, "B_anchor_00340.sfi");
  writeShard(primaryA.cstr(), {{"doc://a", "A", "alpha"}});
  writeShard(primaryB.cstr(), {{"doc://b", "B", "beta"}});
  writeShard(anchorA.cstr(), {{"doc://anchor-a", "Anchor A", "anchor"}});
  writeShard(anchorB.cstr(), {{"doc://anchor-b", "Anchor B", "anchor"}});

  const string indexGroup = joinPath(dir, "index_group_000.txt");
  const string ignoredCache = joinPath(dir, "Z_99999.sfi.loadcache");
  const string lostFound = joinPath(dir, "lost+found");
  writeBytes(indexGroup, "ignored", 7);
  writeBytes(ignoredCache, "ignored", 7);
  EXPECT(mkdir(lostFound.cstr(), 0755) == 0);

  SearchService service(dir, false);
  const vector<string> paths = service.DiscoverShardPaths();
  EXPECT(paths.size() == 4u);
  EXPECT(paths[0] == primaryA);
  EXPECT(paths[1] == anchorA);
  EXPECT(paths[2] == primaryB);
  EXPECT(paths[3] == anchorB);

  EXPECT(!SearchService::IsAnchorShardPath(primaryA));
  EXPECT(!SearchService::IsAnchorShardPath(primaryB));
  EXPECT(SearchService::IsAnchorShardPath(anchorA));
  EXPECT(SearchService::IsAnchorShardPath(anchorB));

  removeShardAndCache(primaryA);
  removeShardAndCache(primaryB);
  removeShardAndCache(anchorA);
  removeShardAndCache(anchorB);
  EXPECT(unlink(indexGroup.cstr()) == 0);
  EXPECT(unlink(ignoredCache.cstr()) == 0);
  EXPECT(rmdir(lostFound.cstr()) == 0);
  EXPECT(rmdir(dir) == 0);
}

void testSolverQueries()
{
  char dirTemplate[] = "/tmp/search_service_test_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shard0 = joinPath(dir, "00000.sfi");
  const string shard1 = joinPath(dir, "00001.sfi");

  writeShard(shard0.cstr(), {{"doc://apollo", "Apollo Doc", "apollo moon landing apple"},
                             {"doc://fruit-a", "Fruit A", "apple banana cherry"}});
  writeShard(shard1.cstr(), {{"doc://brown-fox", "Brown Fox", "brown fox apple"},
                             {"doc://fruit-b", "Fruit B", "brown banana cherry"},
                             {"doc://plain-cherry", "Plain Cherry", "cherry"}});

  SearchService service(dir);

  expectUrls(service.Search("apple", 10), {"doc://apollo", "doc://brown-fox", "doc://fruit-a"});
  const SearchResponse page2 = service.SearchPage("apple", 2, 1);
  EXPECT(page2.Page == 2u);
  EXPECT(page2.Limit == 1u);
  expectUrls(page2, {"doc://brown-fox"});

  expectUrls(service.Search("\"apollo moon landing\"", 10), {"doc://apollo"});
  expectUrls(service.Search("banana cherry", 10), {"doc://fruit-b", "doc://fruit-a"});
  expectUrls(service.Search("apple | brown", 10), {});
  expectUrls(service.Search("brown -fox", 10), {"doc://fruit-b"});
  expectUrls(service.Search("(apple | banana) cherry", 10), {"doc://fruit-a"});

  const SearchResponse limited = service.Search("apple", 1);
  EXPECT(limited.TotalHits == 3u);
  EXPECT(limited.Page == 1u);
  EXPECT(limited.Results.size() == 1u);
  EXPECT(limited.Results[0].Url == string("doc://apollo"));
  EXPECT(limited.Results[0].Score > 0.0);

  const SearchResponse noHit = service.Search("kiwi", 10);
  EXPECT(noHit.Errors.empty());
  EXPECT(noHit.TotalHits == 0u);
  EXPECT(noHit.Results.empty());

  const SearchResponse blank = service.Search("   ", 10);
  EXPECT(blank.Errors.empty());
  EXPECT(blank.TotalHits == 0u);
  EXPECT(blank.Results.empty());

  const SearchResponse parseError = service.Search("(apple", 10);
  EXPECT(parseError.Errors.size() != 0u);
  EXPECT(parseError.Results.empty());

  service.SetQueryExecutionTimeThresholdMs(0);
  const SearchResponse timedOut = service.Search("apple", 10);
  EXPECT(timedOut.Errors.size() == 1u);
  EXPECT(timedOut.Errors[0].Message == string("query timed out after exceeding 0 ms threshold"));
  EXPECT(timedOut.TotalHits == 0u);
  EXPECT(timedOut.Results.empty());

  removeShardAndCache(shard0);
  removeShardAndCache(shard1);
  EXPECT(rmdir(dir) == 0);
}

void testResultDescriptions()
{
  char dirTemplate[] = "/tmp/search_service_description_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  writeShard(shardPath.cstr(),
             {{"doc://description", "Description Doc", "alpha beta",
               "This description should reach the frontend."}});

  SearchService service(dir);
  const SearchResponse response = service.Search("alpha", 10);

  EXPECT(response.Errors.empty());
  EXPECT(response.Results.size() == 1u);
  EXPECT(response.Results[0].Url == string("doc://description"));
  EXPECT(response.Results[0].Description == string("This description should reach the frontend."));

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testPaging()
{
  char dirTemplate[] = "/tmp/search_service_paging_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  for (size_t i = 0; i < 12; ++i)
  {
    char filename[16];
    std::snprintf(filename, sizeof(filename), "%05zu.sfi", i);
    const string shardPath = joinPath(dir, filename);

    char url[32];
    std::snprintf(url, sizeof(url), "doc://page-%02zu", i);
    char title[32];
    std::snprintf(title, sizeof(title), "Page %02zu", i);

    writeShard(shardPath.cstr(), {{url, title, "alpha"}});
  }

  SearchService service(dir);

  const SearchResponse page0 = service.SearchPage("alpha", 0, 10);
  EXPECT(page0.Page == 1u);
  EXPECT(page0.Limit == 10u);
  EXPECT(page0.Results.size() == 10u);
  expectUrls(page0,
             {"doc://page-00", "doc://page-01", "doc://page-02", "doc://page-03", "doc://page-04",
              "doc://page-05", "doc://page-06", "doc://page-07", "doc://page-08", "doc://page-09"});

  const SearchResponse wrapper = service.Search("alpha", 10);
  EXPECT(wrapper.Page == 1u);
  expectUrls(wrapper,
             {"doc://page-00", "doc://page-01", "doc://page-02", "doc://page-03", "doc://page-04",
              "doc://page-05", "doc://page-06", "doc://page-07", "doc://page-08", "doc://page-09"});

  const SearchResponse page2 = service.SearchPage("alpha", 2, 10);
  EXPECT(page2.Page == 2u);
  EXPECT(page2.Limit == 10u);
  EXPECT(page2.Results.size() == 2u);
  expectUrls(page2, {"doc://page-10", "doc://page-11"});

  const SearchResponse overfetch = service.SearchPage("alpha", 1, 30);
  EXPECT(overfetch.Page == 1u);
  EXPECT(overfetch.Limit == 30u);
  EXPECT(overfetch.Results.size() == 12u);

  for (size_t i = 0; i < 12; ++i)
  {
    char filename[16];
    std::snprintf(filename, sizeof(filename), "%05zu.sfi", i);
    removeShardAndCache(joinPath(dir, filename));
  }
  EXPECT(rmdir(dir) == 0);
}

void testResultCap()
{
  char dirTemplate[] = "/tmp/search_service_cap_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  ::Index index;
  for (size_t i = 0; i < 120; ++i)
  {
    char url[32];
    std::snprintf(url, sizeof(url), "doc://cap-%03zu", i);
    char title[32];
    std::snprintf(title, sizeof(title), "Cap %03zu", i);
    string urlString(url);
    string titleString(title);
    index.beginDocument({.url = urlString,
                         .title = titleString,
                         .input = {.domainDepth = 0u,
                                   .depth = 0u,
                                   .urlLength = static_cast<uint16_t>(urlString.size()),
                                   .titleWordCount = countWords(titleString),
                                   .seedSource = 0u}});
    index.ingest("alpha");
    index.endDocument();
  }

  const int fd = open(shardPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  EXPECT(fd >= 0);
  EXPECT(index.serialize(fd) == false);
  EXPECT(close(fd) == 0);

  SearchService service(dir);

  const SearchResponse page10 = service.SearchPage("alpha", 10, 10);
  EXPECT(page10.TotalHits == 100u);
  EXPECT(page10.Results.size() == 10u);
  EXPECT(page10.Results[0].Url == string("doc://cap-090"));
  EXPECT(page10.Results[9].Url == string("doc://cap-099"));

  const SearchResponse page5 = service.SearchPage("alpha", 5, 10);
  EXPECT(page5.TotalHits == 70u);
  EXPECT(page5.Results.size() == 10u);
  EXPECT(page5.Results[0].Url == string("doc://cap-040"));
  EXPECT(page5.Results[9].Url == string("doc://cap-049"));

  const SearchResponse page11 = service.SearchPage("alpha", 11, 10);
  EXPECT(page11.TotalHits == 100u);
  EXPECT(page11.Results.empty());

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testBlankMetadataResultsAreSkipped()
{
  char dirTemplate[] = "/tmp/search_service_blank_metadata_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  writeShard(shardPath.cstr(),
             {{"", "", "apple apple apple"},
              {"https://apple.com/", "Apple", "apple"},
              {"https://allmusic.com/", "AllMusic", "apple"}});

  SearchService service(dir);
  const SearchResponse response = service.Search("apple", 10);

  EXPECT(response.Errors.empty());
  EXPECT(response.TotalHits == 2u);
  EXPECT(response.Results.size() == 2u);
  EXPECT(response.Results[0].Url == string("https://apple.com/"));
  EXPECT(response.Results[0].Title == string("Apple"));
  EXPECT(response.Results[1].Url == string("https://allmusic.com/"));
  EXPECT(response.Results[1].Title == string("AllMusic"));

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testTitleIntentWins()
{
  char dirTemplate[] = "/tmp/search_service_title_intent_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  writeShard(shardPath.cstr(),
             {{"doc://projects", "Development Projects",
               "freebsd projects page with news updates and flash reports"},
              {"doc://newsflash", "FreeBSD News Flash",
               "freebsd archive with news items and flash bulletins"},
              {"doc://generic", "Generic News", "freebsd bulletin with news updates and flash"}});

  SearchService service(dir);
  const SearchResponse response = service.Search("freebsd news flash", 10);

  EXPECT(response.Errors.empty());
  EXPECT(response.Results.size() >= 2u);
  EXPECT(response.Results[0].Url == string("doc://newsflash"));
  EXPECT(response.Results[0].Score > response.Results[1].Score);

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testCompoundTokenQueries()
{
  char dirTemplate[] = "/tmp/search_service_compound_tokens_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shardPath = joinPath(dir, "00000.sfi");
  writeShard(shardPath.cstr(),
             {{"doc://spider-man", "Spider Story", "spider man hero"},
              {"doc://joe-pizza", "Pizza Story", "joe s pizza slice"},
              {"doc://joe-only", "Joe Story", "joe pizza without possessive split"}});

  SearchService service(dir);

  const SearchResponse spider = service.Search("Spider-man", 10);
  EXPECT(spider.Errors.empty());
  expectUrls(spider, {"doc://spider-man"});

  const SearchResponse pizza = service.Search("Joe's Pizza", 10);
  EXPECT(pizza.Errors.empty());
  EXPECT(pizza.Results.size() >= 1u);
  EXPECT(pizza.Results[0].Url == string("doc://joe-pizza"));

  const SearchResponse negated = service.Search("hero -Spider-man", 10);
  EXPECT(negated.Errors.empty());
  EXPECT(negated.Results.empty());

  removeShardAndCache(shardPath);
  EXPECT(rmdir(dir) == 0);
}

void testParallelWorkersMatchSingleThreaded()
{
  char dirTemplate[] = "/tmp/search_service_parallel_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shard0 = joinPath(dir, "00000.sfi");
  const string shard1 = joinPath(dir, "00001.sfi");
  const string shard2 = joinPath(dir, "00002.sfi");
  const string shard3 = joinPath(dir, "00003.sfi");

  writeShard(shard0.cstr(), {{"doc://apollo", "Apollo Doc", "apollo moon landing apple"},
                             {"doc://fruit-a", "Fruit A", "apple banana cherry"}});
  writeShard(shard1.cstr(), {{"doc://brown-fox", "Brown Fox", "brown fox apple"},
                             {"doc://fruit-b", "Fruit B", "brown banana cherry"}});
  writeShard(shard2.cstr(), {{"doc://reference", "Reference API", "documentation reference api"},
                             {"doc://policy", "Privacy Policy", "privacy policy contact"}});
  writeShard(shard3.cstr(), {{"doc://shared", "Shared", "github wikipedia documentation api"},
                             {"doc://weather", "Weather", "news weather forecast"}});

  SearchService service(dir);

  EXPECT(setenv("SEARCH_SHARD_WORKERS", "1", 1) == 0);
  const SearchResponse singleWorker = service.Search("apple | documentation", 10);

  EXPECT(setenv("SEARCH_SHARD_WORKERS", "2", 1) == 0);
  const SearchResponse parallelWorker = service.Search("apple | documentation", 10);

  EXPECT(singleWorker.TotalHits == parallelWorker.TotalHits);
  EXPECT(singleWorker.Results.size() == parallelWorker.Results.size());
  for (size_t i = 0; i < singleWorker.Results.size(); ++i)
  {
    EXPECT(singleWorker.Results[i].Url == parallelWorker.Results[i].Url);
    EXPECT(singleWorker.Results[i].Title == parallelWorker.Results[i].Title);
    EXPECT(nearlyEqual(singleWorker.Results[i].Score, parallelWorker.Results[i].Score));
  }

  EXPECT(unsetenv("SEARCH_SHARD_WORKERS") == 0);
  removeShardAndCache(shard0);
  removeShardAndCache(shard1);
  removeShardAndCache(shard2);
  removeShardAndCache(shard3);
  EXPECT(rmdir(dir) == 0);
}

void testDuplicateUrlsAcrossShards()
{
  char dirTemplate[] = "/tmp/search_service_duplicates_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shard0 = joinPath(dir, "00000.sfi");
  const string shard1 = joinPath(dir, "00001.sfi");

  writeShard(shard0.cstr(), {{"doc://shared", "Shared Title", "documentation api"},
                             {"doc://unique-a", "Unique A", "documentation api"}});
  writeShard(shard1.cstr(), {{"doc://shared", "Shared Title", "documentation api"},
                             {"doc://unique-b", "Unique B", "documentation api"}});

  SearchService service(dir);
  EXPECT(setenv("SEARCH_SHARD_WORKERS", "2", 1) == 0);
  const SearchResponse response = service.Search("documentation api", 10);

  EXPECT(response.TotalHits == 4u);
  expectUrls(response, {"doc://shared", "doc://unique-a", "doc://unique-b"});

  EXPECT(unsetenv("SEARCH_SHARD_WORKERS") == 0);
  removeShardAndCache(shard0);
  removeShardAndCache(shard1);
  EXPECT(rmdir(dir) == 0);
}

void testShardLoadCutoffCanReturnPartialIndex()
{
  char dirTemplate[] = "/tmp/search_service_load_cutoff_XXXXXX";
  char* dir = mkdtemp(dirTemplate);
  EXPECT(dir != nullptr);

  const string shard0 = joinPath(dir, "00000.sfi");
  const string shard1 = joinPath(dir, "00001.sfi");
  writeShard(shard0.cstr(), {{"doc://cutoff-a", "Cutoff A", "alpha"}});
  writeShard(shard1.cstr(), {{"doc://cutoff-b", "Cutoff B", "beta"}});

  EXPECT(setenv("SEARCH_LOAD_TIMEOUT_SEC", "0", 1) == 0);
  SearchService service(dir, false);
  const avengine::search::LoadShardsResult load = service.LoadShards();
  EXPECT(load.TotalShardPaths == 2u);
  EXPECT(load.TimedOut);
  EXPECT(load.LoadedPrimaryShards == 0u);
  EXPECT(load.LoadedAnchorShards == 0u);

  const SearchResponse response = service.Search("alpha", 10);
  EXPECT(response.Errors.empty());
  EXPECT(response.TotalHits == 0u);
  EXPECT(response.Results.empty());

  EXPECT(unsetenv("SEARCH_LOAD_TIMEOUT_SEC") == 0);
  removeShardAndCache(shard0);
  removeShardAndCache(shard1);
  EXPECT(rmdir(dir) == 0);
}

void testServingShardDefaultPathMergesAnchorEvidence()
{
  char rootTemplate[] = "/tmp/search_service_serving_default_XXXXXX";
  char* rootRaw = mkdtemp(rootTemplate);
  EXPECT(rootRaw != nullptr);

  const string input = joinPath(rootRaw, "input");
  const string output = joinPath(rootRaw, "output");
  EXPECT(mkdir(input.cstr(), 0755) == 0);
  EXPECT(mkdir(output.cstr(), 0755) == 0);

  const string primary = joinPath(input.cstr(), "00000.sfi");
  const string anchor = joinPath(input.cstr(), "anchor_00000.sfi");
  writeShard(primary.cstr(), {{"doc://body", "Body Hit", "alpha"},
                              {"doc://anchor", "Anchor Hit", "zzz"}});
  writeShard(anchor.cstr(), {{"doc://anchor", "Anchor Hit", "alpha alpha"}});

  avengine::search::ServingRebuildOptions options;
  options.inputDirectory = input;
  options.outputDirectory = output;
  options.blockSize = 2u;
  avengine::search::ServingRebuildStats stats = {};
  EXPECT(avengine::search::RebuildServingIndex(options, &stats));
  EXPECT(stats.primaryShardCount == 1u);
  EXPECT(stats.anchorShardCount == 1u);
  EXPECT(stats.servingShardCount == 1u);

  const string serving = joinPath(output.cstr(), "serving_00000.ssi");
  SearchService service(output);
  EXPECT(service.PrimaryShardCount() == 1u);
  EXPECT(service.AnchorShardCount() == 0u);

  const SearchResponse response = service.Search("alpha", 10);
  EXPECT(response.Errors.empty());
  EXPECT(response.TotalHits == 2u);
  expectUrls(response, {"doc://anchor", "doc://body"});

  EXPECT(unlink(serving.cstr()) == 0);
  EXPECT(unlink(primary.cstr()) == 0);
  EXPECT(unlink(anchor.cstr()) == 0);
  EXPECT(rmdir(input.cstr()) == 0);
  EXPECT(rmdir(output.cstr()) == 0);
  EXPECT(rmdir(rootRaw) == 0);
}

} // namespace

int main()
{
  testRankingInputRoundTrip();
  testShardLoadCacheCreatedAndReused();
  testShardLoadCacheCorruptionFallsBackAndRewrites();
  testShardLoadCacheStaleSourceFallsBackAndRewrites();
  testPrefixedShardNamesAreDiscoveredAndClassified();
  testSolverQueries();
  testResultDescriptions();
  testPaging();
  testResultCap();
  testBlankMetadataResultsAreSkipped();
  testTitleIntentWins();
  testCompoundTokenQueries();
  testParallelWorkersMatchSingleThreaded();
  testDuplicateUrlsAcrossShards();
  testShardLoadCutoffCanReturnPartialIndex();
  testServingShardDefaultPathMergesAnchorEvidence();
  return 0;
}
