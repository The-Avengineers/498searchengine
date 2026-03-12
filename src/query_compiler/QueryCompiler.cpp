/*
 * QueryCompiler.cpp
 *
 * Implementation of CompileQuery — the full pipeline.
 */

#include <avengine/query_compiler/QueryCompiler.h>
#include <avengine/query_compiler/QueryDecorator.h>
#include <avengine/query_compiler/QueryParser.h>

namespace avengine
{

ParseNode* CompileQuery(const char* queryString)
{
  if (!queryString || queryString[0] == '\0')
    return nullptr;

  QueryParser parser(queryString);
  ParseNode* root = parser.Parse();
  if (!root)
    return nullptr;

  return QueryDecorator::Decorate(root);
}

} // namespace avengine
