#include <avengine/constraint_solver/query_ast.h>

namespace avengine
{
namespace constraint_solver
{

// TODO: Implement DestroyQueryTree(QueryNode*).
// Planned behavior:
// 1. Return immediately on nullptr.
// 2. Recursively delete RequiredChildren, ExcludedChildren, and Children.
// 3. Delete the root node itself.
// 4. Keep cleanup centralized here so parser/solver code stays simple.

} // namespace constraint_solver
} // namespace avengine
