#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/search/search_common.h>
#include <avengine/search/search_service.h>

namespace avengine
{
namespace search
{

struct SearchSource
{
  bool IsPeer = false;
  size_t PeerIndex = 0;
  SearchResponse Response;
};

inline size_t ComputeOverfetchLimit(size_t page, size_t limit)
{
  const size_t pageNormalized = (page == 0) ? 1U : page;
  const size_t limitNormalized = (limit == 0) ? kDefaultSearchLimit : limit;
  return (pageNormalized * limitNormalized) + 20U;
}

inline void AppendTransportError(vector<SearchError>& errors, size_t peerIndex, const char* message)
{
  SearchError err;
  err.Offset = peerIndex;
  err.Message = string(message);
  errors.pushBack(err);
}

inline void PrefixAndAppendErrors(vector<SearchError>& out, const vector<SearchError>& errors,
                                  size_t peerIndex)
{
  for (size_t i = 0; i < errors.size(); ++i)
  {
    SearchError err;
    err.Offset = peerIndex;
    err.Message = errors[i].Message;
    out.pushBack(err);
  }
}

inline SearchResponse MergeResponses(const string& query, size_t page, size_t limit,
                                     const vector<SearchSource>& sources,
                                     const vector<SearchError>& transportErrors, size_t tookMs)
{
  SearchResponse merged;
  merged.Query = query;
  merged.Page = page;
  merged.Limit = limit;
  merged.TookMs = tookMs;

  // Accumulate transport errors first.
  for (size_t i = 0; i < transportErrors.size(); ++i)
    merged.Errors.pushBack(transportErrors[i]);

  // Pool all candidate hits and sum total_hits.
  vector<SearchHit> candidates;
  size_t summedTotalHits = 0;
  for (size_t s = 0; s < sources.size(); ++s)
  {
    const SearchResponse& src = sources[s].Response;
    summedTotalHits += src.TotalHits;

    // Propagate per-source errors.
    PrefixAndAppendErrors(merged.Errors, src.Errors, sources[s].PeerIndex);

    // Accumulate timing from all sources.
    merged.ParseMs += src.ParseMs;
    merged.SolveMs += src.SolveMs;
    merged.CollectMs += src.CollectMs;
    merged.RankMs += src.RankMs;

    for (size_t i = 0; i < src.Results.size(); ++i)
      candidates.pushBack(src.Results[i]);
  }

  // Sort by score descending, then by URL ascending for deterministic ordering.
  candidates.sort(
      [](const SearchHit& a, const SearchHit& b)
      {
        if (a.Score != b.Score)
          return a.Score > b.Score;
        return a.Url < b.Url;
      });

  // O(n) dedup: after sorting, identical URLs have identical scores and are always adjacent.
  vector<SearchHit> uniqueHits;
  for (size_t i = 0; i < candidates.size(); ++i)
  {
    if (uniqueHits.empty() || uniqueHits.back().Url != candidates[i].Url)
    {
      uniqueHits.pushBack(candidates[i]);
      if (uniqueHits.size() >= kMaxVisibleSearchResults)
        break;
    }
  }
  merged.TotalHits =
      summedTotalHits > kMaxVisibleSearchResults ? kMaxVisibleSearchResults : summedTotalHits;

  // Extract the requested page window.
  const size_t pageStart = (page <= 1) ? 0U : (page - 1U) * limit;
  const size_t pageEnd = pageStart + limit;
  for (size_t i = pageStart; i < uniqueHits.size() && i < pageEnd; ++i)
    merged.Results.pushBack(uniqueHits[i]);

  return merged;
}

} // namespace search
} // namespace avengine
