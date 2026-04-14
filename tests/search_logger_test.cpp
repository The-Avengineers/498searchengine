#include <avengine/gradient_descent/search_logger.h>
#include <avengine/ranker/ranker.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace
{

void fail(const char* expr, int line)
{
  fprintf(stderr, "search_logger_test failed at line %d: %s\n", line, expr);
  abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

using avengine::Ranker;
using avengine::string;
using avengine::vector;
using avengine::gradient_descent::SearchLogger;
using avengine::gradient_descent::TrainingDataBuilder;
using avengine::gradient_descent::TrainingSample;

// Returns a temp file path, leaves the file empty and closed.
const char* makeTempFile()
{
  static char path[64];
  strncpy(path, "/tmp/search_logger_test_XXXXXX", sizeof(path));
  int fd = mkstemp(path);
  EXPECT(fd >= 0);
  close(fd);
  return path;
}

Ranker::RankedResult makeResult(const char* url, double score)
{
  Ranker::RankedResult r;
  r.entry.url = string(url);
  r.entry.domain = string("");
  r.entry.depth = 1;
  r.title = string("title");
  r.score = score;
  r.insertionIndex = 0;
  return r;
}

int countLines(const char* path)
{
  FILE* f = fopen(path, "r");
  if (!f)
    return -1;
  int count = 0;
  char buf[65536];
  while (fgets(buf, sizeof(buf), f))
    ++count;
  fclose(f);
  return count;
}

void testNoWriteOnSearch()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);

    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/a", 1.5));
    results.pushBack(makeResult("https://example.com/b", 1.2));
    logger.logSearch(string("apple pie"), results);
  }
  EXPECT(countLines(path) == 0);
  unlink(path);
}

void testOneLineOnClick()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);

    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/a", 1.5));
    results.pushBack(makeResult("https://example.com/b", 1.2));
    results.pushBack(makeResult("https://example.com/c", 0.9));
    string sid = logger.logSearch(string("apple pie"), results);
    logger.logClick(sid, 2);
  }
  EXPECT(countLines(path) == 1);
  unlink(path);
}

void testMultipleClicksWriteMultipleLines()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);

    for (int i = 0; i < 3; ++i)
    {
      vector<Ranker::RankedResult> results;
      results.pushBack(makeResult("https://example.com/a", 1.5));
      results.pushBack(makeResult("https://example.com/b", 1.2));
      string sid = logger.logSearch(string("query"), results);
      logger.logClick(sid, 1);
    }
  }
  EXPECT(countLines(path) == 3);
  unlink(path);
}

void testClickUnknownSessionNoCrash()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    logger.logClick(string("nonexistent_session_id"), 0);
  }
  EXPECT(countLines(path) == 0);
  unlink(path);
}

void testAppendMode()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/a", 1.5));
    results.pushBack(makeResult("https://example.com/b", 1.2));
    string sid = logger.logSearch(string("first query"), results);
    logger.logClick(sid, 1);
  }
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/c", 0.9));
    results.pushBack(makeResult("https://example.com/d", 0.7));
    string sid = logger.logSearch(string("second query"), results);
    logger.logClick(sid, 1);
  }
  EXPECT(countLines(path) == 2);
  unlink(path);
}

void testLineFormat()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/neg", 1.5));
    results.pushBack(makeResult("https://example.com/clicked", 1.2));
    string sid = logger.logSearch(string("apple banana"), results);
    logger.logClick(sid, 1);
  }

  FILE* f = fopen(path, "r");
  EXPECT(f != nullptr);
  char buf[65536];
  EXPECT(fgets(buf, sizeof(buf), f) != nullptr);
  fclose(f);

  EXPECT(strstr(buf, "apple banana") != nullptr);
  EXPECT(strstr(buf, "https://example.com/clicked") != nullptr);
  EXPECT(strstr(buf, "https://example.com/neg") != nullptr);

  int tab_count = 0;
  for (size_t i = 0; buf[i]; ++i)
    if (buf[i] == '\t')
      ++tab_count;
  EXPECT(tab_count >= 4);

  unlink(path);
}

void testOnlyDocsUpToClickWritten()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/rank0", 1.5));
    results.pushBack(makeResult("https://example.com/rank1", 1.2));
    results.pushBack(makeResult("https://example.com/rank2", 0.9));
    string sid = logger.logSearch(string("query"), results);
    logger.logClick(sid, 1);
  }

  FILE* f = fopen(path, "r");
  EXPECT(f != nullptr);
  char buf[65536];
  EXPECT(fgets(buf, sizeof(buf), f) != nullptr);
  fclose(f);

  EXPECT(strstr(buf, "https://example.com/rank0") != nullptr);
  EXPECT(strstr(buf, "https://example.com/rank1") != nullptr);
  EXPECT(strstr(buf, "https://example.com/rank2") == nullptr);

  unlink(path);
}

void testBuilderBasic()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/neg0", 1.5));
    results.pushBack(makeResult("https://example.com/neg1", 1.3));
    results.pushBack(makeResult("https://example.com/pos", 1.1));
    string sid = logger.logSearch(string("test query"), results);
    logger.logClick(sid, 2);
  }

  TrainingDataBuilder builder;
  vector<TrainingSample> samples = builder.build(spath);

  EXPECT(samples.size() == 1);
  EXPECT(samples[0].negative_features.size() == 2);
  unlink(path);
}

// TrainingDataBuilder skips sessions where the top result was clicked
void testBuilderSkipsTopClick()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/a", 1.5));
    results.pushBack(makeResult("https://example.com/b", 1.2));
    string sid = logger.logSearch(string("query"), results);
    logger.logClick(sid, 0);
  }

  TrainingDataBuilder builder;
  vector<TrainingSample> samples = builder.build(spath);

  EXPECT(samples.size() == 0);
  unlink(path);
}

void testBuilderEmptyFile()
{
  string missing("/tmp/does_not_exist_search_logger.log");
  TrainingDataBuilder builder;
  vector<TrainingSample> samples = builder.build(missing);
  EXPECT(samples.size() == 0);
}

void testBuilderMultipleSessions()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);

    {
      vector<Ranker::RankedResult> results;
      results.pushBack(makeResult("https://a.com/0", 1.5));
      results.pushBack(makeResult("https://a.com/1", 1.3));
      results.pushBack(makeResult("https://a.com/2", 1.1));
      string sid = logger.logSearch(string("query one"), results);
      logger.logClick(sid, 2);
    }

    {
      vector<Ranker::RankedResult> results;
      results.pushBack(makeResult("https://b.com/0", 1.5));
      results.pushBack(makeResult("https://b.com/1", 1.2));
      string sid = logger.logSearch(string("query two"), results);
      logger.logClick(sid, 0);
    }

    {
      vector<Ranker::RankedResult> results;
      results.pushBack(makeResult("https://c.com/0", 1.5));
      results.pushBack(makeResult("https://c.com/1", 1.1));
      string sid = logger.logSearch(string("query three"), results);
      logger.logClick(sid, 1);
    }
  }

  TrainingDataBuilder builder;
  vector<TrainingSample> samples = builder.build(spath);

  EXPECT(samples.size() == 2);
  unlink(path);
}

void testSessionIdUniqueness()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<string> ids;
    for (int i = 0; i < 50; ++i)
    {
      vector<Ranker::RankedResult> results;
      results.pushBack(makeResult("https://example.com/a", 1.0));
      ids.pushBack(logger.logSearch(string("q"), results));
    }
    for (size_t i = 0; i < ids.size(); ++i)
      for (size_t j = i + 1; j < ids.size(); ++j)
        EXPECT(!(ids[i] == ids[j]));
  }
  unlink(path);
}

void testClickOutOfBoundsNoCrash()
{
  const char* path = makeTempFile();
  string spath(path);
  {
    SearchLogger logger(spath);
    vector<Ranker::RankedResult> results;
    results.pushBack(makeResult("https://example.com/a", 1.5));
    results.pushBack(makeResult("https://example.com/b", 1.2));
    string sid = logger.logSearch(string("query"), results);
    logger.logClick(sid, 99);
  }
  EXPECT(countLines(path) == 0);
  unlink(path);
}

} // namespace

int main()
{
  testNoWriteOnSearch();
  testOneLineOnClick();
  testMultipleClicksWriteMultipleLines();
  testClickUnknownSessionNoCrash();
  testAppendMode();
  testLineFormat();
  testOnlyDocsUpToClickWritten();
  testBuilderBasic();
  testBuilderSkipsTopClick();
  testBuilderEmptyFile();
  testBuilderMultipleSessions();
  testSessionIdUniqueness();
  testClickOutOfBoundsNoCrash();

  printf("search_logger_test: all tests passed\n");
  return 0;
}
