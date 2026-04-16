#ifndef AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_
#define AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_

#include <chrono>
#include <cstdint>

#include <avengine/constraint_solver/isr_source.h>
#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_parser.h>

namespace avengine
{
namespace constraint_solver
{

struct DocumentMatch
{
  uint32_t DocId{0};
  string Url;
};

struct SolveResult
{
  vector<DocumentMatch> Matches;
  vector<ParseError> Errors;
  bool TimedOut = false;
};

class ConstraintSolver
{
public:
  static constexpr size_t kDefaultExecutionTimeThresholdMs = 2000;
  static constexpr size_t kMaxReturnedMatches = 10000;

  void SetExecutionTimeThresholdMs(size_t thresholdMs);
  size_t GetExecutionTimeThresholdMs() const;
  ParseResult ParseQuery(const string& query) const;
  SolveResult SolveQuery(const string& query, const IsrSource& source) const;
  SolveResult SolveParsed(const QueryNode* root, const IsrSource& source) const;
  SolveResult SolveParsedUntil(const QueryNode* root, const IsrSource& source,
                               std::chrono::steady_clock::time_point deadline) const;

private:
  size_t executionTimeThresholdMs_ = kDefaultExecutionTimeThresholdMs;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_ */
