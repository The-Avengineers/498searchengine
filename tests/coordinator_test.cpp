#include <avengine/search/coordinator_merge.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
void fail(const char* expr, int line)
{
  std::fprintf(stderr, "coordinator_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

using avengine::string;
using avengine::vector;
using avengine::search::ComputeOverfetchLimit;
using avengine::search::IsAvailabilityError;
using avengine::search::MergeResponses;
using avengine::search::SearchHit;
using avengine::search::SearchResponse;
using avengine::search::SearchSource;

void testComputeOverfetchLimit()
{
  // Defaults: page=0 and limit=0 both normalize to 1 and 10 respectively.
  EXPECT(ComputeOverfetchLimit(0, 0) == 30U);
  EXPECT(ComputeOverfetchLimit(1, 10) == 30U);
  EXPECT(ComputeOverfetchLimit(2, 10) == 40U);
  EXPECT(ComputeOverfetchLimit(5, 10) == 70U);
}

SearchHit makeHit(const char* url, double score, const char* description = "")
{
  SearchHit h;
  h.Url = string(url);
  h.Description = string(description);
  h.Score = score;
  return h;
}

SearchSource makeSource(bool isPeer, size_t peerIndex, const vector<SearchHit>& hits,
                        size_t totalHits)
{
  SearchSource src;
  src.IsPeer = isPeer;
  src.PeerIndex = peerIndex;
  src.Response.TotalHits = totalHits;
  for (size_t i = 0; i < hits.size(); ++i)
    src.Response.Results.pushBack(hits[i]);
  return src;
}

void testMergeResponsesTotalHits()
{
  vector<SearchSource> sources;
  vector<SearchHit> hits0;
  hits0.pushBack(makeHit("https://a.com", 2.0));
  hits0.pushBack(makeHit("https://b.com", 1.5));

  vector<SearchHit> hits1;
  hits1.pushBack(makeHit("https://b.com", 1.5));
  hits1.pushBack(makeHit("https://c.com", 1.0));

  sources.pushBack(makeSource(false, 0, hits0, 10));
  sources.pushBack(makeSource(true, 1, hits1, 7));

  vector<avengine::search::SearchError> noErrors;
  const auto merged = MergeResponses(string("q"), 1, 10, sources, noErrors, 5);
  EXPECT(merged.TotalHits == 17U);
  EXPECT(merged.TookMs == 5U);
}

void testMergeResponsesDedup()
{
  // Same URL returned by two sources — should appear only once in merged output.
  vector<SearchHit> hits1;
  hits1.pushBack(makeHit("https://a.com", 2.0));
  hits1.pushBack(makeHit("https://b.com", 1.0));

  vector<SearchHit> hits2;
  hits2.pushBack(makeHit("https://a.com", 2.0)); // duplicate
  hits2.pushBack(makeHit("https://c.com", 0.5));

  vector<SearchSource> sources;
  sources.pushBack(makeSource(false, 0, hits1, 2));
  sources.pushBack(makeSource(true, 1, hits2, 2));

  vector<avengine::search::SearchError> noErrors;
  const auto merged = MergeResponses(string("q"), 1, 10, sources, noErrors, 0);

  // Dedup: a.com appears once, b.com and c.com once each.
  EXPECT(merged.Results.size() == 3U);
  // Highest score first.
  EXPECT(merged.Results[0].Url == string("https://a.com"));
  EXPECT(merged.Results[0].Description == string(""));
}

void testMergeResponsesPreservesDescriptions()
{
  vector<SearchHit> hits;
  hits.pushBack(makeHit("https://a.com", 2.0, "Description from worker"));

  vector<SearchSource> sources;
  sources.pushBack(makeSource(true, 1, hits, 1));

  vector<avengine::search::SearchError> noErrors;
  const auto merged = MergeResponses(string("q"), 1, 10, sources, noErrors, 0);

  EXPECT(merged.Results.size() == 1U);
  EXPECT(merged.Results[0].Url == string("https://a.com"));
  EXPECT(merged.Results[0].Description == string("Description from worker"));
}

void testMergeResponsesTransportErrors()
{
  vector<avengine::search::SearchError> transportErrors;
  avengine::search::AppendTransportError(transportErrors, 1, "timed out waiting for search reply");

  vector<SearchSource> sources;
  sources.pushBack(makeSource(false, 0, vector<SearchHit>{}, 0));

  const auto merged = MergeResponses(string("q"), 1, 10, sources, transportErrors, 0);
  EXPECT(merged.Errors.size() == 0U);
}

void testAvailabilityErrorClassifier()
{
  avengine::search::SearchError loading;
  loading.Message = string("search index loading");
  EXPECT(IsAvailabilityError(loading));

  avengine::search::SearchError timeout;
  timeout.Message = string("query timed out after exceeding 2000 ms threshold");
  EXPECT(IsAvailabilityError(timeout));

  avengine::search::SearchError parse;
  parse.Message = string("unterminated phrase");
  EXPECT(!IsAvailabilityError(parse));
}

void testMergeResponsesSuppressesWorkerLoadingWhenClusterIsDegraded()
{
  vector<SearchHit> hits;
  hits.pushBack(makeHit("https://a.com", 2.0));

  SearchSource local = makeSource(false, 0, hits, 1);
  SearchSource worker = makeSource(true, 1, vector<SearchHit>{}, 0);
  worker.Response.Errors.pushBack({0, string("search index loading")});

  vector<SearchSource> sources;
  sources.pushBack(local);
  sources.pushBack(worker);

  vector<avengine::search::SearchError> noErrors;
  const auto merged = MergeResponses(string("q"), 1, 10, sources, noErrors, 0);
  EXPECT(merged.Errors.size() == 0U);
  EXPECT(merged.Results.size() == 1U);
  EXPECT(merged.Results[0].Url == string("https://a.com"));
}

void testMergeResponsesCollapsesFullClusterAvailabilityFailure()
{
  vector<avengine::search::SearchError> transportErrors;
  avengine::search::AppendTransportError(transportErrors, 1, "failed to send search request");
  avengine::search::AppendTransportError(transportErrors, 2, "timed out waiting for search reply");

  SearchSource local;
  local.IsPeer = false;
  local.PeerIndex = 0;
  local.Response.Errors.pushBack({0, string("query timed out after exceeding 2000 ms threshold")});

  SearchSource worker;
  worker.IsPeer = true;
  worker.PeerIndex = 3;
  worker.Response.Errors.pushBack({0, string("search index loading")});

  vector<SearchSource> sources;
  sources.pushBack(local);
  sources.pushBack(worker);

  const auto merged = MergeResponses(string("q"), 1, 10, sources, transportErrors, 0);
  EXPECT(merged.Errors.size() == 1U);
  EXPECT(merged.Errors[0].Message == string("search cluster unavailable"));
}

void testMergeResponsesKeepsNonAvailabilityErrorsVisible()
{
  SearchSource local = makeSource(false, 0, vector<SearchHit>{}, 0);
  local.Response.Errors.pushBack({0, string("unterminated phrase")});

  vector<avengine::search::SearchError> transportErrors;
  avengine::search::AppendTransportError(transportErrors, 1, "failed to send search request");

  vector<SearchSource> sources;
  sources.pushBack(local);

  const auto merged = MergeResponses(string("q"), 1, 10, sources, transportErrors, 0);
  EXPECT(merged.Errors.size() == 2U);
  EXPECT(merged.Errors[0].Message == string("unterminated phrase"));
  EXPECT(merged.Errors[1].Message == string("search cluster unavailable"));
}

void testMergeResponsesPagination()
{
  // Build 20 hits with descending scores.
  vector<SearchHit> hits;
  for (int i = 20; i >= 1; --i)
  {
    char url[64];
    std::snprintf(url, sizeof(url), "https://example.com/%d", i);
    hits.pushBack(makeHit(url, static_cast<double>(i)));
  }

  vector<SearchSource> sources;
  sources.pushBack(makeSource(false, 0, hits, 20));

  vector<avengine::search::SearchError> noErrors;

  // Page 1, limit 10: items 1..10 (scores 20..11).
  const auto p1 = MergeResponses(string("q"), 1, 10, sources, noErrors, 0);
  EXPECT(p1.Results.size() == 10U);
  EXPECT(p1.Results[0].Score == 20.0);
  EXPECT(p1.Results[9].Score == 11.0);

  // Page 2, limit 10: items 11..20 (scores 10..1).
  const auto p2 = MergeResponses(string("q"), 2, 10, sources, noErrors, 0);
  EXPECT(p2.Results.size() == 10U);
  EXPECT(p2.Results[0].Score == 10.0);
  EXPECT(p2.Results[9].Score == 1.0);
}

void testMergeResponsesCapAtOneHundred()
{
  vector<SearchHit> hits;
  for (int i = 150; i >= 1; --i)
  {
    char url[64];
    std::snprintf(url, sizeof(url), "https://example.com/%d", i);
    hits.pushBack(makeHit(url, static_cast<double>(i)));
  }

  vector<SearchSource> sources;
  sources.pushBack(makeSource(false, 0, hits, 150));

  vector<avengine::search::SearchError> noErrors;

  const auto page10 = MergeResponses(string("q"), 10, 10, sources, noErrors, 0);
  EXPECT(page10.TotalHits == 100U);
  EXPECT(page10.Results.size() == 10U);
  EXPECT(page10.Results[0].Score == 60.0);
  EXPECT(page10.Results[9].Score == 51.0);

  const auto page11 = MergeResponses(string("q"), 11, 10, sources, noErrors, 0);
  EXPECT(page11.TotalHits == 100U);
  EXPECT(page11.Results.size() == 0U);
}

} // namespace

int main()
{
  testComputeOverfetchLimit();
  testMergeResponsesTotalHits();
  testMergeResponsesDedup();
  testMergeResponsesPreservesDescriptions();
  testAvailabilityErrorClassifier();
  testMergeResponsesTransportErrors();
  testMergeResponsesSuppressesWorkerLoadingWhenClusterIsDegraded();
  testMergeResponsesCollapsesFullClusterAvailabilityFailure();
  testMergeResponsesKeepsNonAvailabilityErrorsVisible();
  testMergeResponsesPagination();
  testMergeResponsesCapAtOneHundred();
  return 0;
}
