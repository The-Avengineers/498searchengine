/*
 * QueryParser.cpp
 *
 * Implementation of QueryParser.h
 */

#include <avengine/query_compiler/QueryParser.h>

namespace avengine
{

QueryParser::QueryParser(const char* input) : lexer(input) {}

ParseNode* QueryParser::ParseOrExpr()
{
  ParseNode* left = ParseAndExpr();
  if (!left)
    return nullptr;

  vector<ParseNode*> orChildren;
  orChildren.pushBack(left);

  while (lexer.Match(TokenType::OR))
  {
    ParseNode* right = ParseAndExpr();
    if (!right)
    {
      for (size_t i = 0; i < orChildren.size(); ++i)
        delete orChildren[i];
      return nullptr;
    }
    orChildren.pushBack(right);
  }

  if (orChildren.size() == 1)
    return orChildren[0];
  return new OrNode(orChildren);
}

ParseNode* QueryParser::ParseAndExpr()
{
  ParseNode* left = ParseNotPrefix();
  if (!left)
    return nullptr;

  vector<ParseNode*> andChildren;
  vector<ParseNode*> excluded;
  andChildren.pushBack(left);

  while (lexer.Match(TokenType::AND))
  {
    ParseNode* right = ParseNotPrefix();
    if (!right)
    {
      for (size_t i = 0; i < andChildren.size(); ++i)
        delete andChildren[i];
      for (size_t i = 0; i < excluded.size(); ++i)
        delete excluded[i];
      return nullptr;
    }
    // Pull NotNodes out as exclusions
    NotNode* notNode = dynamic_cast<NotNode*>(right);
    if (notNode)
    {
      excluded.pushBack(notNode->child);
      notNode->child = nullptr;
      delete notNode;
    }
    else
    {
      andChildren.pushBack(right);
    }
  }

  if (andChildren.size() == 1 && excluded.empty())
    return andChildren[0];
  return new AndNode(andChildren, excluded);
}

ParseNode* QueryParser::ParseNotPrefix()
{
  if (lexer.Match(TokenType::NOT))
  {
    ParseNode* inner = ParseAtom();
    if (!inner)
      return nullptr;
    return new NotNode(inner);
  }
  return ParseAtom();
}

ParseNode* QueryParser::ParseAtom()
{
  if (lexer.Match(TokenType::LPAREN))
  {
    ParseNode* inner = ParseOrExpr();
    if (!inner)
      return nullptr;
    if (!lexer.Match(TokenType::RPAREN))
    {
      delete inner;
      return nullptr;
    }
    return inner;
  }

  if (lexer.Peek().type == TokenType::PHRASE_START)
  {
    return ParsePhrase();
  }

  if (lexer.Peek().type == TokenType::WORD)
  {
    Token tok = lexer.Consume();
    return new WordNode(tok.value);
  }

  return nullptr;
}

PhraseNode* QueryParser::ParsePhrase()
{
  if (!lexer.Match(TokenType::PHRASE_START))
    return nullptr;

  vector<WordNode*> words;
  while (lexer.Peek().type == TokenType::WORD)
  {
    Token tok = lexer.Consume();
    words.pushBack(new WordNode(tok.value));
  }

  if (!lexer.Match(TokenType::PHRASE_END))
  {
    for (size_t i = 0; i < words.size(); ++i)
      delete words[i];
    return nullptr;
  }

  if (words.empty())
    return nullptr;

  return new PhraseNode(words);
}

ParseNode* QueryParser::Parse()
{
  if (lexer.AllConsumed())
    return nullptr;

  ParseNode* root = ParseOrExpr();
  if (!root)
    return nullptr;

  if (dynamic_cast<NotNode*>(root))
  {
    delete root;
    return nullptr;
  }

  if (!lexer.AllConsumed())
  {
    delete root;
    return nullptr;
  }

  return root;
}

} // namespace avengine
