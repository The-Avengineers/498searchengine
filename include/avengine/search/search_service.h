#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/constraint_solver/constraint_solver.h>
#include <avengine/ranker/ranker.h>

namespace avengine
{
namespace search
{

struct SearchError
{
  size_t Offset = 0;
  string Message;
};

struct SearchHit
{
  string Url;
  string Title;
  double Score = 0.0;
};

struct SearchResponse
{
  string Query;
  size_t Page = 1;
  size_t Limit = 10;
  size_t TotalHits = 0;
  size_t TookMs = 0;
  vector<SearchError> Errors;
  vector<SearchHit> Results;
};

class IndexShardSource;

class SearchService
{
public:
  explicit SearchService(const string& indexDirectory = string("."));
  ~SearchService();

  SearchService(const SearchService&) = delete;
  SearchService& operator=(const SearchService&) = delete;

  void SetQueryExecutionTimeThresholdMs(size_t thresholdMs);
  size_t GetQueryExecutionTimeThresholdMs() const;
  SearchResponse Search(const string& query, size_t limit = 10) const;
  SearchResponse SearchPage(const string& query, size_t page = 1, size_t limit = 10) const;

private:
  void LoadShards();

  string indexDirectory_;
  constraint_solver::ConstraintSolver solver_;
  vector<IndexShardSource*> shards_;
  ConstantScoreStrategy defaultStrategy_;
};

} // namespace search
} // namespace avengine
