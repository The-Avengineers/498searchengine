#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_

#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_lexer.h>

namespace avengine
{
namespace constraint_solver
{

class QueryParser
{
public:
  explicit QueryParser(const string& input);

  ParseResult Parse();

private:
  QueryNode* ParseOrExpression();
  QueryNode* ParseConjunction();
  QueryNode* ParseUnary(bool& excluded);
  QueryNode* ParsePrimary();

  bool Match(QueryTokenKind kind);
  void AddError(size_t offset, const char* message);

  QueryLexer Lexer;
  QueryToken Current;
  ParseResult Result;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_PARSER_H_ */
