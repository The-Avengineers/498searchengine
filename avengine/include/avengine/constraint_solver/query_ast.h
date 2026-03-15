#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>

namespace avengine
{
namespace constraint_solver
{

// Kinds of nodes the parser can build.
enum class QueryNodeKind
{
  Invalid,
  Term,
  Phrase,
  Conjunction,
  Or,
};

// One AST node for the query tree.
// Only some fields matter depending on Kind.
struct QueryNode
{
  QueryNodeKind Kind{QueryNodeKind::Invalid};
  string Value;
  vector<string> PhraseTerms;
  vector<QueryNode*> RequiredChildren;
  vector<QueryNode*> ExcludedChildren;
  vector<QueryNode*> Children;
};

// Basic parse error with source offset.
struct ParseError
{
  size_t Offset{0};
  string Message;
};

// What comes back from parsing.
struct ParseResult
{
  QueryNode* Root{nullptr};
  vector<ParseError> Errors;
};

// Deletes the whole tree rooted at root.
void DestroyQueryTree(QueryNode* root);

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_ */
