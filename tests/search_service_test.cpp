#include <avengine/indexer/indexer.h>

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
    index.beginDocument({.url = doc.Url, .title = doc.Title, .input = 0});
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

  expectUrls(service.Search("apple", 10), {"doc://apollo", "doc://fruit-a", "doc://brown-fox"});
  expectUrls(service.Search("\"apollo moon landing\"", 10), {"doc://apollo"});
  expectUrls(service.Search("banana cherry", 10), {"doc://fruit-a", "doc://fruit-b"});
  expectUrls(service.Search("apple | brown", 10),
             {"doc://apollo", "doc://fruit-a", "doc://brown-fox", "doc://fruit-b"});
  expectUrls(service.Search("brown -fox", 10), {"doc://fruit-b"});
  expectUrls(service.Search("(apple | banana) cherry", 10), {"doc://fruit-a", "doc://fruit-b"});

  const SearchResponse limited = service.Search("apple", 1);
  EXPECT(limited.TotalHits == 3u);
  EXPECT(limited.Results.size() == 1u);
  EXPECT(limited.Results[0].Url == string("doc://apollo"));
  EXPECT(limited.Results[0].Score == 1.0);

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

} // namespace

int main()
{
  testSolverQueries();
  return 0;
}
