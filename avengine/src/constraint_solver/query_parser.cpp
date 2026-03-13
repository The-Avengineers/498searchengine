#include <avengine/constraint_solver/query_parser.h>

namespace avengine
{
namespace constraint_solver
{
namespace
{
bool startsUnary(QueryTokenKind kind)
{
  return kind == QueryTokenKind::Word || kind == QueryTokenKind::Phrase ||
         kind == QueryTokenKind::LeftParen || kind == QueryTokenKind::Minus;
}

QueryNode* makeNode(QueryNodeKind kind)
{
  QueryNode* node = new QueryNode();
  node->Kind = kind;
  return node;
}

void appendOrChild(QueryNode* parent, QueryNode* child)
{
  if (child == nullptr)
  {
    return;
  }

  if (child->Kind == QueryNodeKind::Or)
  {
    for (QueryNode* grandchild : child->Children)
    {
      parent->Children.pushBack(grandchild);
    }
    child->Children.clear();
    delete child;
    return;
  }

  parent->Children.pushBack(child);
}

vector<string> splitPhraseTerms(const string& phrase)
{
  vector<string> terms;
  string current;

  for (size_t i = 0; i < phrase.size(); ++i)
  {
    const char c = phrase[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
    {
      current.pushBack(c);
    }
    else if (current.size() > 0)
    {
      terms.pushBack(current);
      current = string();
    }
  }

  if (current.size() > 0)
  {
    terms.pushBack(current);
  }

  return terms;
}

bool isStandaloneExclusion(const QueryNode* node)
{
  return node != nullptr && node->Kind == QueryNodeKind::Conjunction &&
         node->RequiredChildren.empty() && !node->ExcludedChildren.empty();
}
} // namespace

QueryParser::QueryParser(const string& input) : Lexer(input)
{
  Current = Lexer.Next();
}

// Parse() processes operators in order from highest to lowest: Primary, Unary, Conjunction, Or

ParseResult QueryParser::Parse()
{
  Result.Root = nullptr;
  Result.Errors.clear();

  QueryNode* root = ParseOrExpression();

  if (root == nullptr && Result.Errors.empty())
  {
    AddError(Current.Offset, "expected a term, phrase, or group");
  }

  if (root != nullptr && isStandaloneExclusion(root))
  {
    AddError(0, "a query cannot be only exclusions");
  }

  if (root != nullptr && Current.Kind != QueryTokenKind::End)
  {
    AddError(Current.Offset, "unexpected trailing token");
  }

  if (!Result.Errors.empty())
  {
    DestroyQueryTree(root);
    root = nullptr;
  }

  Result.Root = root;
  return Result;
}

QueryNode* QueryParser::ParseOrExpression()
{
  QueryNode* first = ParseConjunction();
  if (first == nullptr)
  {
    AddError(Current.Offset, "expected a term, phrase, or group");
    return nullptr;
  }

  if (Current.Kind != QueryTokenKind::Or)
  {
    return first;
  }

  QueryNode* node = makeNode(QueryNodeKind::Or);
  appendOrChild(node, first);

  while (Match(QueryTokenKind::Or))
  {
    QueryNode* next = ParseConjunction();
    if (next == nullptr)
    {
      AddError(Current.Offset, "expected a term, phrase, or group after '|'");
      break;
    }
    appendOrChild(node, next);
  }

  if (node->Children.size() == 1)
  {
    QueryNode* only = node->Children[0];
    node->Children.clear();
    delete node;
    return only;
  }

  return node;
}

QueryNode* QueryParser::ParseConjunction()
{
  vector<QueryNode*> required;
  vector<QueryNode*> excluded;

  while (startsUnary(Current.Kind))
  {
    bool isExcluded = false;
    QueryNode* child = ParseUnary(isExcluded);
    if (child == nullptr)
    {
      break;
    }

    if (isExcluded)
    {
      excluded.pushBack(child);
    }
    else
    {
      required.pushBack(child);
    }
  }

  if (required.empty() && excluded.empty())
  {
    return nullptr;
  }

  if (excluded.empty() && required.size() == 1)
  {
    return required[0];
  }

  QueryNode* node = makeNode(QueryNodeKind::Conjunction);
  node->RequiredChildren = required;
  node->ExcludedChildren = excluded;
  return node;
}

QueryNode* QueryParser::ParseUnary(bool& excluded)
{
  excluded = Match(QueryTokenKind::Minus);
  QueryNode* node = ParsePrimary();
  if (node == nullptr && excluded)
  {
    AddError(Current.Offset, "expected something after '-'");
  }
  return node;
}

QueryNode* QueryParser::ParsePrimary()
{
  if (Current.Kind == QueryTokenKind::Word)
  {
    QueryNode* node = makeNode(QueryNodeKind::Term);
    node->Value = Current.Lexeme;
    Current = Lexer.Next();
    return node;
  }

  if (Current.Kind == QueryTokenKind::Phrase)
  {
    QueryNode* node = makeNode(QueryNodeKind::Phrase);
    node->PhraseTerms = splitPhraseTerms(Current.Lexeme);
    Current = Lexer.Next();

    if (node->PhraseTerms.empty())
    {
      AddError(Current.Offset, "empty phrase is not allowed");
      DestroyQueryTree(node);
      return nullptr;
    }

    return node;
  }

  if (Current.Kind == QueryTokenKind::LeftParen)
  {
    const size_t groupOffset = Current.Offset;
    Current = Lexer.Next();

    if (Current.Kind == QueryTokenKind::RightParen)
    {
      AddError(groupOffset, "empty group is not allowed");
      Current = Lexer.Next();
      return nullptr;
    }

    QueryNode* inner = ParseOrExpression();
    if (!Match(QueryTokenKind::RightParen))
    {
      AddError(groupOffset, "missing ')'");
      DestroyQueryTree(inner);
      return nullptr;
    }
    return inner;
  }

  if (Current.Kind == QueryTokenKind::Invalid)
  {
    AddError(Current.Offset, "invalid token in query");
    Current = Lexer.Next();
    return nullptr;
  }

  return nullptr;
}

bool QueryParser::Match(QueryTokenKind kind)
{
  if (Current.Kind != kind)
  {
    return false;
  }

  Current = Lexer.Next();
  return true;
}

void QueryParser::AddError(size_t offset, const char* message)
{
  ParseError error;
  error.Offset = offset;
  error.Message = string(message);
  Result.Errors.pushBack(error);
}

} // namespace constraint_solver
} // namespace avengine
