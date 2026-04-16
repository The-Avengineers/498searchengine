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

inline bool MessageEquals(const string& message, const char* literal)
{
  size_t length = 0;
  while (literal[length] != '\0')
    ++length;

  if (message.size() != length)
    return false;

  for (size_t i = 0; i < length; ++i)
  {
    if (message[i] != literal[i])
      return false;
  }
  return true;
}

inline bool MessageStartsWith(const string& message, const char* prefix)
{
  size_t length = 0;
  while (prefix[length] != '\0')
    ++length;

  if (message.size() < length)
    return false;

  for (size_t i = 0; i < length; ++i)
  {
    if (message[i] != prefix[i])
      return false;
  }
  return true;
}

inline bool MessageEndsWith(const string& message, const char* suffix)
{
  size_t suffixLength = 0;
  while (suffix[suffixLength] != '\0')
    ++suffixLength;

  if (message.size() < suffixLength)
    return false;

  const size_t start = message.size() - suffixLength;
  for (size_t i = 0; i < suffixLength; ++i)
  {
    if (message[start + i] != suffix[i])
      return false;
  }
  return true;
}

inline size_t ComputeOverfetchLimit(size_t page, size_t limit)
{
  const size_t pageNormalized = (page == 0) ? 1U : page;
  const size_t limitNormalized = (limit == 0) ? kDefaultSearchLimit : limit;
  return (pageNormalized * limitNormalized) + 20U;
}

inline bool IsAvailabilityError(const SearchError& error)
{
  if (MessageEquals(error.Message, "failed to send search request") ||
      MessageEquals(error.Message, "timed out waiting for search reply") ||
      MessageEquals(error.Message, "search index loading"))
  {
    return true;
  }

  return MessageStartsWith(error.Message, "query timed out after exceeding ") &&
         MessageEndsWith(error.Message, " ms threshold");
}

inline bool IsSuccessfulSourceResponse(const SearchResponse& response)
{
  return response.Errors.empty();
}

inline void AppendTransportError(vector<SearchError>& errors, size_t peerIndex, const char* message)
{
  SearchError err;
  err.Offset = peerIndex;
  err.Message = string(message);
  errors.pushBack(err);
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

  size_t successfulSourceCount = 0;
  for (size_t s = 0; s < sources.size(); ++s)
  {
    if (IsSuccessfulSourceResponse(sources[s].Response))
      ++successfulSourceCount;
  }

  const bool suppressAvailabilityErrors = successfulSourceCount >= 1;
  bool sawAvailabilityError = false;

  // Transport problems are cluster-availability problems. Keep them out of the
  // user-visible payload when at least one source answered cleanly.
  for (size_t i = 0; i < transportErrors.size(); ++i)
  {
    if (IsAvailabilityError(transportErrors[i]))
    {
      sawAvailabilityError = true;
      continue;
    }
    merged.Errors.pushBack(transportErrors[i]);
  }

  // Pool all candidate hits and sum total_hits.
  vector<SearchHit> candidates;
  size_t summedTotalHits = 0;
  for (size_t s = 0; s < sources.size(); ++s)
  {
    const SearchResponse& src = sources[s].Response;
    summedTotalHits += src.TotalHits;

    // Propagate per-source errors, but treat loading / timeout / unreachable
    // states as cluster availability instead of user-facing query errors.
    for (size_t i = 0; i < src.Errors.size(); ++i)
    {
      if (IsAvailabilityError(src.Errors[i]))
      {
        sawAvailabilityError = true;
        if (suppressAvailabilityErrors)
          continue;
        continue;
      }

      SearchError err;
      err.Offset = sources[s].PeerIndex;
      err.Message = src.Errors[i].Message;
      merged.Errors.pushBack(err);
    }

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

  if (successfulSourceCount == 0 && sawAvailabilityError)
  {
    SearchError err;
    err.Offset = 0;
    err.Message = string("search cluster unavailable");
    merged.Errors.pushBack(err);
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
