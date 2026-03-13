#include <avengine/constraint_solver/constraint_solver.h>

namespace avengine
{
namespace constraint_solver
{
ParseResult ConstraintSolver::ParseQuery(const string& query)
{
  QueryParser parser(query);
  return parser.Parse();
}

} // namespace constraint_solver
} // namespace avengine
