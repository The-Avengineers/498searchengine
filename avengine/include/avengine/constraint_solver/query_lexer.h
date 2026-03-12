#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>

namespace avengine
{
namespace constraint_solver
{

// Token categories recognized by the query lexer.
enum class QueryTokenKind
{
  Invalid,
  End,
  Word,
  Phrase,
  Or,
  Minus,
  LeftParen,
  RightParen,
};

// Single token emitted by the lexer.
struct QueryToken
{
  QueryTokenKind Kind{QueryTokenKind::Invalid};
  string Lexeme;
  size_t Offset{0};
};

// Converts a raw query string into a stream of query tokens.
class QueryLexer
{
public:
  explicit QueryLexer(const string& input);

  // TODO: Return the next token without consuming it.
  QueryToken Peek() const;
  // TODO: Consume and return the next token, handling words, phrases, and operators.
  QueryToken Next();
  // TODO: Report whether the lexer has consumed the full input.
  bool AllConsumed() const;

private:
  string Input;
  size_t Offset{0};
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_ */
