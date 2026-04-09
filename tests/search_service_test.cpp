#include <avengine/indexer/indexer.h>

#include <avengine/search/index_shard_source.h>
#include <avengine/search/search_service.h>

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

void writeShard(const char* path, std::initializer_list<TestDocument> docs)
{
  ::Index index;
  for (const TestDocument& doc : docs)
  {
    string url(doc.Url);
    string title(doc.Title);
    index.beginDocument({.url = url,
                         .title = title,
                         .input = ranking_input::Pack(0u, static_cast<uint32_t>(url.size()),
                                                      ranking_input::CountWords(title))});
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

  const RankingInput expected =
      ranking_input::Pack(0u, static_cast<uint32_t>(string("doc://static").size()),
                          ranking_input::CountWords(string("Static Title")));
  EXPECT(info->rankingInput == expected);
  EXPECT(ranking_input::Depth(info->rankingInput) == 0u);
  EXPECT(ranking_input::UrlLength(info->rankingInput) == 12u);
  EXPECT(ranking_input::TitleWordCount(info->rankingInput) == 2u);

  EXPECT(unlink(shardPath.cstr()) == 0);
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

  expectUrls(service.Search("apple", 10), {"doc://brown-fox", "doc://apollo", "doc://fruit-a"});
  expectUrls(service.Search("\"apollo moon landing\"", 10), {"doc://apollo"});
  expectUrls(service.Search("banana cherry", 10), {"doc://fruit-b", "doc://fruit-a"});
  expectUrls(service.Search("apple | brown", 10),
             {"doc://brown-fox", "doc://apollo", "doc://fruit-b", "doc://fruit-a"});
  expectUrls(service.Search("brown -fox", 10), {"doc://fruit-b"});
  expectUrls(service.Search("(apple | banana) cherry", 10), {"doc://fruit-a", "doc://fruit-b"});

  const SearchResponse limited = service.Search("apple", 1);
  EXPECT(limited.TotalHits == 3u);
  EXPECT(limited.Results.size() == 1u);
  EXPECT(limited.Results[0].Url == string("doc://brown-fox"));
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

  EXPECT(unlink(shard0.cstr()) == 0);
  EXPECT(unlink(shard1.cstr()) == 0);
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

  for (size_t i = 0; i < 12; ++i)
  {
    char filename[16];
    std::snprintf(filename, sizeof(filename), "%05zu.sfi", i);
    EXPECT(unlink(joinPath(dir, filename).cstr()) == 0);
  }
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

  EXPECT(unlink(shardPath.cstr()) == 0);
  EXPECT(rmdir(dir) == 0);
}

} // namespace

int main()
{
  testRankingInputRoundTrip();
  testSolverQueries();
  testPaging();
  testTitleIntentWins();
  return 0;
}
