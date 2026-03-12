#ifndef AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_
#define AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_

#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_parser.h>

namespace avengine
{
namespace constraint_solver
{

// Facade for the constraint-solver component.
// For now it only exposes query parsing.
class ConstraintSolver
{
public:
  // TODO: Call QueryParser and return its ParseResult.
  // Later this class can also own AST-to-ISR lowering hooks.
  ParseResult ParseQuery(const string& query);
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_CONSTRAINT_SOLVER_H_ */
