#include <avengine/constraint_solver/query_ast.h>

namespace avengine
{
namespace constraint_solver
{
namespace
{
void destroyNodeList(vector<QueryNode*>& nodes)
{
  for (QueryNode* node : nodes)
  {
    DestroyQueryTree(node);
  }
}
} // namespace

void DestroyQueryTree(QueryNode* root)
{
  if (root == nullptr)
  {
    return;
  }

  destroyNodeList(root->RequiredChildren);
  destroyNodeList(root->ExcludedChildren);
  destroyNodeList(root->Children);
  delete root;
}

} // namespace constraint_solver
} // namespace avengine
