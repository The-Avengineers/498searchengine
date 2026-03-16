#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>

namespace avengine
{
namespace constraint_solver
{

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

struct QueryToken
{
  QueryTokenKind Kind{QueryTokenKind::Invalid};
  string Lexeme;
  size_t Offset{0};
};

class QueryLexer
{
public:
  explicit QueryLexer(const string& input);

  QueryToken Peek() const;
  QueryToken Next();
  bool AllConsumed() const;

private:
  string Input;
  size_t Offset{0};
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_LEXER_H_ */
