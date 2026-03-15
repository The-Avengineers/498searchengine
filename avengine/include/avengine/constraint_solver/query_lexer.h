#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>

namespace avengine
{
namespace constraint_solver
{

// Token kinds the lexer knows about.
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

// One token from the query text.
struct QueryToken
{
  QueryTokenKind Kind{QueryTokenKind::Invalid};
  string Lexeme;
  size_t Offset{0};
};

// Small lexer for words, phrases, and query operators.
class QueryLexer
{
public:
  explicit QueryLexer(const string& input);

  // Look at the next token without moving forward.
  QueryToken Peek() const;
  // Consume and return the next token.
  QueryToken Next();
  // True when there is nothing left except whitespace.
  bool AllConsumed() const;

private:
  string Input;
  size_t Offset{0};
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_ */
