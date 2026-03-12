#include <avengine/constraint_solver/query_lexer.h>

namespace avengine
{
namespace constraint_solver
{

// TODO: Implement QueryLexer.
// Planned behavior:
// 1. Skip insignificant whitespace between tokens.
// 2. Emit tokens for '|', '-', '(' and ')'.
// 3. Parse quoted phrases as a single Phrase token while preserving the raw text.
// 4. Parse plain terms as Word tokens using the same normalization rules as indexing.
// 5. Track token offsets for error reporting in the parser.
// 6. Expose Peek(), Next(), and AllConsumed() without using STL containers.

} // namespace constraint_solver
} // namespace avengine
