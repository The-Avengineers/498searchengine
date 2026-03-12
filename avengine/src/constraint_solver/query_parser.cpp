#include <avengine/constraint_solver/query_parser.h>

namespace avengine
{
namespace constraint_solver
{

// TODO: Implement QueryParser.
// Planned behavior:
// 1. Use recursive descent with grammar roughly:
//    Query := OrExpr
//    OrExpr := Conjunction ( '|' Conjunction )*
//    Conjunction := Unary+
//    Unary := '-'? Primary
//    Primary := WORD | PHRASE | '(' OrExpr ')'
// 2. Build QueryNode trees using avengine::string and avengine::vector only.
// 3. Treat adjacency as implicit conjunction.
// 4. Reject invalid standalone exclusions such as '-fox'.
// 5. Flatten repeated OR/conjunction groups where it simplifies the AST.
// 6. Record useful ParseError entries with source offsets instead of crashing.

} // namespace constraint_solver
} // namespace avengine
