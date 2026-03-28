#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/constraint_solver/constraint_solver.h>

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
  size_t Limit = 10;
  size_t TotalHits = 0;
  size_t TookMs = 0;
  vector<SearchError> Errors;
  vector<SearchHit> Results;
};

class SearchService
{
public:
  explicit SearchService(const string& indexDirectory = string("."));

  SearchResponse Search(const string& query, size_t limit = 10) const;

private:
  string indexDirectory_;
  constraint_solver::ConstraintSolver solver_;
};

} // namespace search
} // namespace avengine
