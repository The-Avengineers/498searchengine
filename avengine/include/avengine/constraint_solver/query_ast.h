#ifndef AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_
#define AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_

#include <cstddef>

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>

namespace avengine
{
namespace constraint_solver
{

// Kinds of syntax tree nodes produced by the query parser.
enum class QueryNodeKind
{
  Invalid,
  Term,
  Phrase,
  Conjunction,
  Or,
};

// Generic syntax tree node for the query language.
// Different fields are used depending on Kind.
struct QueryNode
{
  QueryNodeKind Kind{QueryNodeKind::Invalid};
  string Value;
  vector<string> PhraseTerms;
  vector<QueryNode*> RequiredChildren;
  vector<QueryNode*> ExcludedChildren;
  vector<QueryNode*> Children;
};

// Parser error with source offset and human-readable message.
struct ParseError
{
  size_t Offset{0};
  string Message;
};

// Result of parsing a query into a syntax tree.
struct ParseResult
{
  QueryNode* Root{nullptr};
  vector<ParseError> Errors;
};

// TODO: Implement recursive cleanup of the entire syntax tree.
// This should delete all children owned by root and safely accept nullptr.
void DestroyQueryTree(QueryNode* root);

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_QUERY_AST_H_ */
