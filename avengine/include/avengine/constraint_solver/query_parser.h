#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_

#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_lexer.h>

namespace avengine
{
namespace constraint_solver
{

// Recursive descent parser for the constraint solver query language.
class QueryParser
{
public:
  explicit QueryParser(const string& input);

  // TODO: Parse the full query and return either an AST or parse errors.
  ParseResult Parse();

private:
  // TODO: Parse OR chains and flatten adjacent OR nodes into one Or node.
  QueryNode* ParseOrExpression();
  // TODO: Parse implicit conjunctions and split required vs excluded children.
  QueryNode* ParseConjunction();
  // TODO: Parse unary exclusion using '-'.
  QueryNode* ParseUnary();
  // TODO: Parse a word, phrase, or parenthesized subexpression.
  QueryNode* ParsePrimary();

  // TODO: Consume the current token when it matches the requested kind.
  bool Match(QueryTokenKind kind);
  // TODO: Append a structured parse error to Result.Errors.
  void AddError(size_t offset, const char* message);

  QueryLexer Lexer;
  QueryToken Current;
  ParseResult Result;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_ */
