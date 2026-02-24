#include <avengine/ranker/Strategy.h>
#include <avengine/ranker/ranker.h>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unistd.h>

namespace
{
void fail(const char* expr, int line)
{
  std::fprintf(stderr, "ranker_test failed at line %d: %s\n", line, expr);
  std::abort();
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

bool nearlyEqual(double a, double b, double eps = 1e-9)
{
  return std::fabs(a - b) <= eps;
}

class ConstantStrategy : public Ranker::Strategy
{
public:
  explicit ConstantStrategy(double value) : Value(value) {}

  double Score(const string& url, const vector<vector<size_t>>& term_positions) const override
  {
    (void)url;
    (void)term_positions;
    return Value;
  }

private:
  double Value;
};

class UrlLengthStrategy : public Ranker::Strategy
{
public:
  double Score(const string& url, const vector<vector<size_t>>& term_positions) const override
  {
    (void)term_positions;
    return static_cast<double>(url.size());
  }
};

string writeTempHtml(const char* html)
{
  char pathTemplate[] = "/tmp/ranker_title_test_XXXXXX.html";
  const int fd = mkstemps(pathTemplate, 5);
  EXPECT(fd >= 0);
  close(fd);

  std::ofstream out(pathTemplate, std::ios::binary);
  out.write(html, static_cast<std::streamsize>(std::strlen(html)));
  out.close();
  return string(pathTemplate);
}

void testRankerMethods()
{
  Ranker ranker;
  vector<vector<size_t>> positions;

  // Empty strategy list should produce zero score.
  EXPECT(nearlyEqual(ranker.Score("https://a.com", positions), 0.0));

  // Add and combine weighted strategies.
  ConstantStrategy one(1.0);
  ConstantStrategy two(2.0);
  ranker.AddStrategy(&one, 0.5);
  ranker.AddStrategy(&two, 2.0);
  EXPECT(nearlyEqual(ranker.Score("https://a.com", positions), 4.5));

  // nullptr strategy returns 0.0 by current implementation.
  ranker.AddStrategy(nullptr, 1.0);
  EXPECT(nearlyEqual(ranker.Score("https://a.com", positions), 0.0));

  ranker.ClearStrategies();
  EXPECT(nearlyEqual(ranker.Score("https://a.com", positions), 0.0));

  // Consider() + TopResults(): keep top 10 in descending score order.
  UrlLengthStrategy len;
  ranker.AddStrategy(&len, 1.0);
  for (int i = 0; i < 12; ++i)
  {
    string url("https://example.com/");
    for (int j = 0; j < i; ++j)
      url.pushBack('x');
    ranker.Consider(url, positions);
  }

  const vector<Ranker::RankedResult>& top = ranker.TopResults();
  EXPECT(top.size() == 10);
  for (size_t i = 1; i < top.size(); ++i)
  {
    EXPECT(top[i - 1].score >= top[i].score);
  }
  EXPECT(top[0].score > top[top.size() - 1].score);
}

void testDomainStrat()
{
  DomainStrat strat;
  vector<vector<size_t>> positions;
  EXPECT(nearlyEqual(strat.Score("https://university.edu/page", positions), 1.0));
  EXPECT(nearlyEqual(strat.Score("https://agency.gov/home", positions), 1.0));
  EXPECT(nearlyEqual(strat.Score("https://example.com", positions), 0.0));
}

void testShortURLStrat()
{
  ShortURLStrat strat;
  vector<vector<size_t>> positions;
  EXPECT(nearlyEqual(strat.Score("", positions), 0.0));
  EXPECT(nearlyEqual(strat.Score("abcd", positions), 0.25));
}

void testShortTitleStrat()
{
  ShortTitleStrat strat;
  vector<vector<size_t>> positions;

  string shortTitle =
      writeTempHtml("<html><head><title>one two three</title></head><body></body></html>");
  string longTitle = writeTempHtml(
      "<html><head><title>1 2 3 4 5 6 7 8 9 10 11</title></head><body></body></html>");

  EXPECT(nearlyEqual(strat.Score(shortTitle, positions), 1.0));
  EXPECT(nearlyEqual(strat.Score(longTitle, positions), 0.0));
  EXPECT(nearlyEqual(strat.Score("/tmp/path/does/not/exist/ranker_test.html", positions), 0.0));

  EXPECT(unlink(shortTitle.cstr()) == 0);
  EXPECT(unlink(longTitle.cstr()) == 0);
}

void testExactPhraseMatch()
{
  ExactPhraseMatch strat;
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{}), 1.0));
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{2}}), 1.0));
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{10}, {11}}), 1.0));
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{10}, {15}}), 0.0));
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{10}, {}}), 0.0));
}

void testCorrectOrdering()
{
  CorrectOrdering strat;
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{}), 0.0));
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{4}}), 1.0));

  // Perfect 3-term ordered run with no slack.
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{1}, {2}, {3}}), 1.0));

  // Only a 2-term ordered run exists.
  const double partial = strat.Score("unused", vector<vector<size_t>>{{3}, {1}, {2}});
  EXPECT(partial > 0.0 && partial < 1.0);

  // No forward ordering among any adjacent terms.
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{{3}, {2}, {1}}), 0.0));
}

void testUrlDepth()
{
  UrlDepth strat;
  vector<vector<size_t>> positions;
  EXPECT(nearlyEqual(strat.Score("", positions), 0.0));
  EXPECT(nearlyEqual(strat.Score("https://example.com/a/b", positions), 0.25));
}

void testHttpsMatch()
{
  HttpsMatch strat;
  Ranker::Strategy& asBase = strat;
  vector<vector<size_t>> positions;

  EXPECT(nearlyEqual(asBase.Score("https://secure.example.com", positions), 1.0));
  EXPECT(nearlyEqual(asBase.Score("http://plain.example.com", positions), 0.0));
}

void testQueryOccurrence()
{
  QueryOccurrence strat(vector<string>{"foo", "bar"});
  vector<vector<size_t>> positions;

  EXPECT(nearlyEqual(strat.Score("https://example.com/foobar", positions), 1.0));
  const double separated = strat.Score("https://example.com/foo___bar", positions);
  EXPECT(separated > 0.0 && separated < 1.0);
  EXPECT(nearlyEqual(strat.Score("https://example.com/baz", positions), 0.0));
}

void testEarlyQueryTermMatch()
{
  EarlyQueryTermMatch strat;
  const double score = strat.Score("unused", vector<vector<size_t>>{{0}, {2}});
  EXPECT(nearlyEqual(score, 0.75));
  EXPECT(nearlyEqual(strat.Score("unused", vector<vector<size_t>>{}), 0.0));
}
} // namespace

int main()
{
  testRankerMethods();
  testDomainStrat();
  testShortURLStrat();
  testShortTitleStrat();
  testExactPhraseMatch();
  testCorrectOrdering();
  testUrlDepth();
  testHttpsMatch();
  testQueryOccurrence();
  testEarlyQueryTermMatch();
  return 0;
}
