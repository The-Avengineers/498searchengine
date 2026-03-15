#ifndef AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_
#define AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_

#include <cstdint>

#include <avengine/constraint_solver/posting_source.h>
#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_parser.h>

namespace avengine
{
namespace constraint_solver
{

// One matching document.
struct DocumentMatch
{
  uint32_t DocId{0};
  string Url;
};

// What comes back from solving a query.
struct SolveResult
{
  vector<DocumentMatch> Matches;
  vector<ParseError> Errors;
};

// Entry point for parsing and document-level query solving.
class ConstraintSolver
{
public:
  // Parse one query into a syntax tree.
  ParseResult ParseQuery(const string& query) const;

  // Parse and solve one raw query string.
  SolveResult SolveQuery(const string& query, const PostingSource& source) const;

  // Solve an already-built query tree.
  SolveResult SolveParsed(const QueryNode* root, const PostingSource& source) const;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_ */
