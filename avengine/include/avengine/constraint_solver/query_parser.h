#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_

#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_lexer.h>

namespace avengine
{
namespace constraint_solver
{

// Small recursive-descent parser for the query language.
class QueryParser
{
public:
  explicit QueryParser(const string& input);

  // Parse the full query and return either a tree or errors.
  ParseResult Parse();

private:
  // Parse OR chains.
  QueryNode* ParseOrExpression();
  // Parse implicit AND groups.
  QueryNode* ParseConjunction();
  // Parse an optional leading '-'.
  QueryNode* ParseUnary(bool& excluded);
  // Parse a term, phrase, or parenthesized group.
  QueryNode* ParsePrimary();

  // Consume the current token if it matches.
  bool Match(QueryTokenKind kind);
  // Add one parse error.
  void AddError(size_t offset, const char* message);

  QueryLexer Lexer;
  QueryToken Current;
  ParseResult Result;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_ */
