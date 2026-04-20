#include <avengine/constraint_solver/constraint_solver.h>

#include <avengine/constraint_solver/isr_tree.h>
#include <avengine/constraint_solver/stop_words.h>

#include <chrono>
#include <limits>

namespace avengine
{
namespace constraint_solver
{
namespace
{
struct IsrExecutionState
{
  vector<ISR*> OwnedNodes;
  ISREndDoc* DocumentEnd = nullptr;

  ~IsrExecutionState()
  {
    for (ISR* node : OwnedNodes)
    {
      delete node;
    }
  }

  template <typename T> T* Own(T* node)
  {
    if (node)
      OwnedNodes.pushBack(node);
    return node;
  }
};

uint32_t documentIdFromPost(const Post* post, ISREndDoc* documentEnd)
{
  if (!post)
    return std::numeric_limits<uint32_t>::max();

  const Attributes attributes = post->GetAttributes();
  if (attributes.kind == Attributes::Kind::WORD)
    return attributes.Word.documentId;
  if (attributes.kind == Attributes::Kind::DOCUMENT)
    return attributes.Document.documentId;

  if (!documentEnd || !documentEnd->Seek(post->GetEndLocation()))
    return std::numeric_limits<uint32_t>::max();

  return documentEnd->GetCurrentDocumentId();
}

ISR* BuildExecutionTree(const QueryNode* node, const IsrSource& source, IsrExecutionState* state)
{
  if (!node || !state)
    return nullptr;

  switch (node->Kind)
  {
  case QueryNodeKind::Term:
    return state->Own(source.OpenTermIsr(node->Value));

  case QueryNodeKind::Phrase:
  {
    vector<ISR*> terms;
    for (const string& term : node->PhraseTerms)
    {
      ISRWord* child = state->Own(source.OpenTermIsr(term));
      if (!child)
      {
        return nullptr;
      }
      terms.pushBack(child);
    }

    if (terms.empty())
      return nullptr;
    if (terms.size() == 1)
      return terms[0];

    return state->Own(new ISRPhrase(terms, state->DocumentEnd));
  }

  case QueryNodeKind::Conjunction:
  {
    vector<ISR*> required;
    for (const QueryNode* child : node->RequiredChildren)
    {
      ISR* requiredChild = BuildExecutionTree(child, source, state);
      if (!requiredChild)
      {
        return nullptr;
      }
      required.pushBack(requiredChild);
    }

    vector<ISR*> excluded;
    for (const QueryNode* child : node->ExcludedChildren)
    {
      ISR* excludedChild = BuildExecutionTree(child, source, state);
      if (excludedChild)
      {
        excluded.pushBack(excludedChild);
      }
    }

    if (required.empty())
      return nullptr;
    if (excluded.empty())
    {
      if (required.size() == 1)
        return required[0];
      return state->Own(new ISRAnd(required, state->DocumentEnd));
    }

    return state->Own(new ISRAndNot(required, excluded, state->DocumentEnd));
  }

  case QueryNodeKind::Or:
  {
    vector<ISR*> children;
    for (const QueryNode* child : node->Children)
    {
      ISR* builtChild = BuildExecutionTree(child, source, state);
      if (builtChild)
      {
        children.pushBack(builtChild);
      }
    }

    if (children.empty())
      return nullptr;
    if (children.size() == 1)
      return children[0];

    return state->Own(new ISROr(children));
  }

  case QueryNodeKind::Invalid:
  default:
    return nullptr;
  }
}
// Counts term nodes that are not stop words, just counts does not edit any nodes
int countNonStopTerms(const QueryNode* node)
{
  if (!node)
    return 0;

  switch (node->Kind)
  {
  case QueryNodeKind::Term:
    return isStopWord(node->Value) ? 0 : 1;

  case QueryNodeKind::Phrase:
    return 1;

  case QueryNodeKind::Conjunction:
  {
    int count = 0;
    for (const QueryNode* c : node->RequiredChildren)
      count += countNonStopTerms(c);
    return count;
  }

  case QueryNodeKind::Or:
  {
    int count = 0;
    for (const QueryNode* c : node->Children)
      count += countNonStopTerms(c);
    return count;
  }

  default:
    return 0;
  }
}

// Strips stop word term nodes from children in place.
// Returns true if this node should be kept
bool applyStopWordFilter(QueryNode* node)
{
  if (!node)
    return false;

  switch (node->Kind)
  {
  case QueryNodeKind::Term:
    return !isStopWord(node->Value);

  case QueryNodeKind::Phrase:
    return true;

  case QueryNodeKind::Conjunction:
  {
    vector<QueryNode*> kept;
    for (QueryNode* c : node->RequiredChildren)
    {
      if (applyStopWordFilter(c))
        kept.pushBack(c);
      else
        DestroyQueryTree(c);
    }
    node->RequiredChildren = kept;
    return !node->RequiredChildren.empty() || !node->ExcludedChildren.empty();
  }

  case QueryNodeKind::Or:
  {
    vector<QueryNode*> kept;
    for (QueryNode* c : node->Children)
    {
      if (applyStopWordFilter(c))
        kept.pushBack(c);
      else
        DestroyQueryTree(c);
    }
    node->Children = kept;
    return !node->Children.empty();
  }

  default:
    return true;
  }
}

} // namespace

void ConstraintSolver::SetExecutionTimeThresholdMs(size_t thresholdMs)
{
  executionTimeThresholdMs_ = thresholdMs;
}

size_t ConstraintSolver::GetExecutionTimeThresholdMs() const
{
  return executionTimeThresholdMs_;
}

ParseResult ConstraintSolver::ParseQuery(const string& query) const
{
  QueryParser parser(query);
  return parser.Parse();
}

SolveResult ConstraintSolver::SolveQuery(const string& query, const IsrSource& source) const
{
  SolveResult result = {};

  ParseResult parsed = ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    result.Errors = parsed.Errors;
    DestroyQueryTree(parsed.Root);
    return result;
  }

  if (countNonStopTerms(parsed.Root) >= 2)
    applyStopWordFilter(parsed.Root);

  result = SolveParsed(parsed.Root, source);
  DestroyQueryTree(parsed.Root);
  return result;
}

SolveResult ConstraintSolver::SolveParsed(const QueryNode* root, const IsrSource& source) const
{
  const auto startedAt = std::chrono::steady_clock::now();
  return SolveParsedUntil(root, source,
                          startedAt + std::chrono::milliseconds(executionTimeThresholdMs_));
}

SolveResult ConstraintSolver::SolveParsedUntil(const QueryNode* root, const IsrSource& source,
                                               std::chrono::steady_clock::time_point deadline) const
{
  SolveResult result = {};
  if (!root)
  {
    return result;
  }

  const auto timedOut = [deadline]() { return std::chrono::steady_clock::now() >= deadline; };

  IsrExecutionState state;
  state.DocumentEnd = state.Own(source.OpenEndDocIsr());
  if (!state.DocumentEnd)
  {
    return result;
  }

  ISR* executionRoot = BuildExecutionTree(root, source, &state);
  if (!executionRoot)
  {
    return result;
  }

  if (timedOut())
  {
    result.TimedOut = true;
    return result;
  }

  uint32_t lastDocId = std::numeric_limits<uint32_t>::max();
  size_t matchCount = 0;
  for (Post* match = executionRoot->Seek(1); match != nullptr;)
  {
    if ((matchCount++ & 63) == 0 && timedOut())
    {
      result.TimedOut = true;
      return result;
    }

    const uint32_t docId = documentIdFromPost(match, state.DocumentEnd);
    if (docId != std::numeric_limits<uint32_t>::max() && docId != lastDocId)
    {
      result.Matches.pushBack({docId, source.GetDocumentURL(docId)});
      lastDocId = docId;
      if (result.Matches.size() >= ConstraintSolver::kMaxReturnedMatches)
        return result;
    }

    if (!state.DocumentEnd->Seek(match->GetEndLocation()))
    {
      break;
    }

    const Location nextDocumentTarget = state.DocumentEnd->GetEndLocation() + 1;
    match = executionRoot->Seek(nextDocumentTarget);
  }

  return result;
}

} // namespace constraint_solver
} // namespace avengine
