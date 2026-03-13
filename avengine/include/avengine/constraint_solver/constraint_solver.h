#ifndef AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_
#define AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_

#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_parser.h>

namespace avengine
{
namespace constraint_solver
{

// Thin entry point for the constraint-solver component.
class ConstraintSolver
{
public:
  // Parse one query into a syntax tree.
  ParseResult ParseQuery(const string& query);
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_ */
