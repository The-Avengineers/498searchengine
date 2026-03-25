#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>

namespace avengine
{
namespace constraint_solver
{

enum class QueryNodeKind
{
  Invalid,
  Term,
  Phrase,
  Conjunction,
  Or,
};

struct QueryNode
{
  QueryNodeKind Kind{QueryNodeKind::Invalid};
  string Value;
  vector<string> PhraseTerms;
  vector<QueryNode*> RequiredChildren;
  vector<QueryNode*> ExcludedChildren;
  vector<QueryNode*> Children;
};

struct ParseError
{
  size_t Offset{0};
  string Message;
};

struct ParseResult
{
  QueryNode* Root{nullptr};
  vector<ParseError> Errors;
};

void DestroyQueryTree(QueryNode* root);

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_ */
