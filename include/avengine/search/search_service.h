#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/constraint_solver/constraint_solver.h>
#include <avengine/ranker/ranker_weights.h>

#include <cstdio>

namespace avengine
{
namespace search
{

static constexpr size_t kSearchTierCount = 3;

struct SearchError
{
  size_t Offset = 0;
  string Message;
};

struct SearchHit
{
  string Url;
  string Title;
  string Description;
  double Score = 0.0;
};

struct SearchResponse
{
  string Query;
  size_t Page = 1;
  size_t Limit = 10;
  size_t TotalHits = 0;
  size_t TookMs = 0;
  // Per-phase timing breakdowns (sum across all shards)
  size_t ParseMs = 0;
  size_t SolveMs = 0;
  size_t CollectMs = 0;
  size_t RankMs = 0;
  vector<SearchError> Errors;
  vector<SearchHit> Results;
};

struct LoadShardsResult
{
  size_t TotalShardPaths = 0;
  size_t LoadedPrimaryShards = 0;
  size_t LoadedAnchorShards = 0;
  size_t FailedShardLoads = 0;
  size_t TookMs = 0;
  bool TimedOut = false;
};

class IndexShardSource;

class SearchService
{
public:
  explicit SearchService(const string& indexDirectory = string("."), bool loadShards = true);
  ~SearchService();

  SearchService(const SearchService&) = delete;
  SearchService& operator=(const SearchService&) = delete;

  void SetQueryExecutionTimeThresholdMs(size_t thresholdMs);
  size_t GetQueryExecutionTimeThresholdMs() const;
  LoadShardsResult LoadShards();
  vector<string> DiscoverShardPaths() const;
  static bool IsAnchorShardPath(const string& path);
  void ResetLoadedShards();
  void AppendLoadedShard(IndexShardSource* shard, bool isAnchorShard);
  size_t PrimaryShardCount() const;
  size_t AnchorShardCount() const;
  SearchResponse Search(const string& query, size_t limit = 10) const;
  SearchResponse SearchPage(const string& query, size_t page = 1, size_t limit = 10) const;

private:
  string indexDirectory_;
  constraint_solver::ConstraintSolver solver_;
  vector<IndexShardSource*> shardsByTier_[kSearchTierCount];
  vector<IndexShardSource*> anchorShardsByTier_[kSearchTierCount];
};

} // namespace search
} // namespace avengine
