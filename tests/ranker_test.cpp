#include <avengine/ranker/Strategy.h>
#include <avengine/ranker/ranker.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
using avengine::urlEntry;
using avengine::vector;

bool nearlyEqual(double a, double b, double eps = 1e-9)
{
  return std::fabs(a - b) <= eps;
}

urlEntry makeEntry(const char* url, const char* domain = "", unsigned int depth = 0)
{
  urlEntry entry;
  entry.url = url;
  entry.domain = domain;
  entry.depth = static_cast<int>(depth);
  return entry;
}

class ConstantStrategy : public Ranker::Strategy
{
public:
  explicit ConstantStrategy(double value) : Value(value) {}

  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const override
  {
    (void)entry;
    (void)term_positions;
    (void)signals;
    return Value;
  }

private:
  double Value;
};

class UrlLengthStrategy : public Ranker::Strategy
{
public:
  double Score(const urlEntry& entry, const vector<vector<size_t>>& term_positions,
               const Ranker::RankSignals& signals) const override
  {
    (void)term_positions;
    (void)signals;
    return static_cast<double>(entry.url.size());
  }
};

void testRankerMethods()
{
  Ranker ranker;
  vector<vector<size_t>> positions;
  Ranker::RankSignals signals;

  // Empty strategy list should produce zero score.
  EXPECT(nearlyEqual(ranker.Score(makeEntry("https://a.com"), positions, signals), 0.0));

  // Add and combine weighted strategies.
  ConstantStrategy one(1.0);
  ConstantStrategy two(2.0);
  ranker.AddStrategy(&one, 0.5);
  ranker.AddStrategy(&two, 2.0);
  EXPECT(nearlyEqual(ranker.Score(makeEntry("https://a.com"), positions, signals), 4.5));

  // nullptr strategy returns 0.0 by current implementation.
  ranker.AddStrategy(nullptr, 1.0);
  EXPECT(nearlyEqual(ranker.Score(makeEntry("https://a.com"), positions, signals), 0.0));

  ranker.ClearStrategies();
  EXPECT(nearlyEqual(ranker.Score(makeEntry("https://a.com"), positions, signals), 0.0));

  UrlLengthStrategy len;

  // Custom capacity should trim the result set before it ever grows past the bound.
  Ranker cappedRanker(3);
  cappedRanker.AddStrategy(&len, 1.0);
  for (int i = 0; i < 5; ++i)
  {
    urlEntry entry = makeEntry("https://example.com/");
    for (int j = 0; j < i; ++j)
      entry.url.pushBack('x');
    cappedRanker.Consider(entry, string(), positions, signals);
  }
  EXPECT(cappedRanker.TopResults().size() == 3u);

  // Consider() + TopResults(): keep top 10 in descending score order.
  ranker.AddStrategy(&len, 1.0);
  for (int i = 0; i < 12; ++i)
  {
    urlEntry entry = makeEntry("https://example.com/");
    for (int j = 0; j < i; ++j)
      entry.url.pushBack('x');
    ranker.Consider(entry, string(), positions, signals);
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
  Ranker::RankSignals signals;
  // top tier
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("https://university.edu/page", "university.edu"), positions, signals),
      1.0));
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("https://agency.gov/home", "agency.gov"), positions, signals), 1.0));
  // top tier (.org moved here)
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("https://charity.org/about", "charity.org"), positions, signals), 1.0));
  // mid tier
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("https://example.com", "example.com"), positions, signals), 0.6));
  // low tier
  EXPECT(
      nearlyEqual(strat.Score(makeEntry("https://site.xyz", "site.xyz"), positions, signals), 0.3));
}

void testShortURLStrat()
{
  ShortURLStrat strat;
  vector<vector<size_t>> positions;
  Ranker::RankSignals signals;
  // empty url
  EXPECT(nearlyEqual(strat.Score(makeEntry(""), positions, signals), 1.0));
  // short url (len 20 <= 40)
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com/"), positions, signals), 1.0));
  // mid-range url (len 80, midpoint of [40,120]) => score = (120-80)/(120-40) = 0.5
  const char* mid_url =
      "https://example.com/some/path/to/a/page/that/is/eighty/characters/in/total/xxxxx";
  EXPECT(std::strlen(mid_url) == 80);
  EXPECT(nearlyEqual(strat.Score(makeEntry(mid_url), positions, signals), 0.5));
  // very long url (>= 120)
  const char* long_url =
      "https://example.com/very/long/path/to/a/page/that/exceeds/one/hundred/and/"
      "twenty/characters/in/total/definitely/over/the/threshold/yes";
  EXPECT(std::strlen(long_url) >= 120);
  EXPECT(nearlyEqual(strat.Score(makeEntry(long_url), positions, signals), 0.0));
}

void testShortTitleStrat()
{
  ShortTitleStrat strat;
  vector<vector<size_t>> positions;
  Ranker::RankSignals signals;

  // short title (3 words <= 5)
  signals.title_word_count = 3;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), positions, signals), 1.0));
  // long title (16 words >= 15)
  signals.title_word_count = 16;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), positions, signals), 0.0));
  // mid-range title (10 words) => (15 - 10) / (15 - 5) = 0.5
  signals.title_word_count = 10;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), positions, signals), 0.5));
  // zero words
  signals.title_word_count = 0;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), positions, signals), 1.0));
}

void testExactPhraseMatch()
{
  ExactPhraseMatch strat;
  Ranker::RankSignals signals;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{}, signals), 1.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{2}}, signals), 1.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{10}, {11}}, signals),
                     1.0));
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{3, 10}, {4, 20}, {5, 21}}, signals),
      1.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{10}, {15}}, signals),
                     0.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{10}, {}}, signals),
                     0.0));

  vector<vector<size_t>> title_positions = {{1}, {2}};
  signals.title_term_positions = &title_positions;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{10}, {15}}, signals),
                     0.9));
}

void testCorrectOrdering()
{
  CorrectOrdering strat;
  Ranker::RankSignals signals;
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{}, signals), 0.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{4}}, signals), 1.0));

  // Perfect 3-term ordered run with no slack.
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{1}, {2}, {3}}, signals), 1.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"),
                                 vector<vector<size_t>>{{10, 20}, {15, 21}, {16, 22}}, signals),
                     1.0));

  // Only a 2-term ordered run exists.
  const double partial =
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{3}, {1}, {2}}, signals);
  EXPECT(partial > 0.0 && partial < 1.0);

  // No forward ordering among any adjacent terms.
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{3}, {2}, {1}}, signals), 0.0));

  vector<vector<size_t>> title_positions = {{1}, {2}, {3}};
  signals.title_term_positions = &title_positions;
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{3}, {2}, {1}}, signals), 0.8));
}

void testUrlDepth()
{
  UrlDepth strat;
  vector<vector<size_t>> positions;
  Ranker::RankSignals signals;
  // depth 0 (seed) and depth 2 are within shallow_thres → 1.0
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com/a/b", "", 0), positions, signals),
                     1.0));
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com/a/b", "", 2), positions, signals),
                     1.0));
  // depth 5 is midpoint of [2,8] → (8-5)/(8-2) = 0.5
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com/a/b", "", 5), positions, signals),
                     0.5));
  // depth 10 is beyond deep_thres → 0.0
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com/deep", "", 10), positions, signals),
                     0.0));
}

void testOutlinkCount()
{
  OutlinkCount strat;
  vector<vector<size_t>> positions;
  Ranker::RankSignals signals;

  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com"), positions, signals), 0.0));

  signals.outlink_count = 3;
  const double small = strat.Score(makeEntry("https://example.com"), positions, signals);
  EXPECT(small > 0.0 && small < 1.0);

  signals.outlink_count = 32;
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com"), positions, signals), 1.0));

  signals.outlink_count = 64;
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com"), positions, signals), 1.0));
}

void testQueryOccurrence()
{
  QueryOccurrence strat(vector<string>{"foo", "bar"});
  vector<vector<size_t>> positions;
  Ranker::RankSignals signals;

  EXPECT(
      nearlyEqual(strat.Score(makeEntry("https://example.com/foobar"), positions, signals), 1.0));
  const double separated =
      strat.Score(makeEntry("https://example.com/foo___bar"), positions, signals);
  EXPECT(separated > 0.0 && separated < 1.0);
  const double reversed = strat.Score(makeEntry("https://example.com/bar/foo"), positions, signals);
  EXPECT(reversed > 0.5 && reversed < 1.0);
  EXPECT(nearlyEqual(strat.Score(makeEntry("https://example.com/baz"), positions, signals), 0.0));
}

void testEarlyQueryTermMatch()
{
  EarlyQueryTermMatch strat;
  Ranker::RankSignals no_title_signals;
  const double body_score =
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{0}, {2}}, no_title_signals);
  EXPECT(nearlyEqual(body_score, 0.75));
  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{}, no_title_signals),
                     0.0));

  vector<vector<size_t>> partial_title_positions = {{1}, {}};
  Ranker::RankSignals partial_title_signals;
  partial_title_signals.title_term_positions = &partial_title_positions;
  const double partial =
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{0}, {2}}, partial_title_signals);
  EXPECT(nearlyEqual(partial, 0.75 + (0.7 / std::log2(3.0))));

  vector<vector<size_t>> full_title_positions = {{1}, {3}};
  Ranker::RankSignals full_title_signals;
  full_title_signals.title_term_positions = &full_title_positions;
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{0}, {2}}, full_title_signals),
      0.75 + 0.7 * ((1.0 / std::log2(3.0) + 1.0 / std::log2(5.0)) / 2.0)));
}

void testTermCoverage()
{
  TermCoverage strat;
  Ranker::RankSignals signals;

  EXPECT(nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{}, signals), 0.0));
  EXPECT(nearlyEqual(
      strat.Score(makeEntry("unused"), vector<vector<size_t>>{{1}, {}, {4}}, signals), 2.0 / 3.0));

  vector<vector<size_t>> title_positions = {{1}, {}, {2}};
  signals.title_term_positions = &title_positions;
  EXPECT(
      nearlyEqual(strat.Score(makeEntry("unused"), vector<vector<size_t>>{{1}, {}, {4}}, signals),
                  1.0666666666666667));
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
  testOutlinkCount();
  testQueryOccurrence();
  testEarlyQueryTermMatch();
  testTermCoverage();
  return 0;
}
