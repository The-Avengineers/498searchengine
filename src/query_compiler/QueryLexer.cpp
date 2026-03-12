/*
 * QueryLexer.cpp
 *
 * Implementation of QueryLexer.h
 *
 * Scans a raw query string left to right, emitting tokens.
 * Whitespace between terms becomes implicit AND.
 */

#include <avengine/query_compiler/QueryLexer.h>

#include <cctype>

namespace avengine
{

static bool IsWordChar(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '\'';
}

static string SubStr(const char* s, size_t start, size_t len)
{
  string result;
  for (size_t i = 0; i < len; ++i)
    result.pushBack(s[start + i]);
  return result;
}

bool QueryLexer::NeedsImplicitAnd(TokenType prev, TokenType cur)
{
  bool prevIsValue = (prev == TokenType::WORD || prev == TokenType::RPAREN ||
                      prev == TokenType::PHRASE_END);
  bool curIsValue = (cur == TokenType::WORD || cur == TokenType::LPAREN ||
                     cur == TokenType::PHRASE_START || cur == TokenType::NOT);
  return prevIsValue && curIsValue;
}

QueryLexer::QueryLexer(const char* input) : pos(0)
{
  // Trim leading trailing whitespace
  size_t inputLen = 0;
  while (input[inputLen] != '\0')
    ++inputLen;

  size_t trimStart = 0;
  while (trimStart < inputLen && std::isspace(static_cast<unsigned char>(input[trimStart])))
    ++trimStart;
  size_t trimEnd = inputLen;
  while (trimEnd > trimStart && std::isspace(static_cast<unsigned char>(input[trimEnd - 1])))
    --trimEnd;

  const char* src = input;
  size_t srcLen = trimEnd;

  vector<Token> raw;

  size_t i = trimStart;
  while (i < srcLen)
  {
    char c = src[i];

    if (std::isspace(static_cast<unsigned char>(c)))
    {
      ++i;
      continue;
    }

    if (c == '|')
    {
      raw.pushBack(Token(TokenType::OR));
      ++i;
      continue;
    }

    if (c == '(')
    {
      raw.pushBack(Token(TokenType::LPAREN));
      ++i;
      continue;
    }
    if (c == ')')
    {
      raw.pushBack(Token(TokenType::RPAREN));
      ++i;
      continue;
    }

    if (c == '-' && i + 1 < srcLen &&
        !std::isspace(static_cast<unsigned char>(src[i + 1])))
    {
      raw.pushBack(Token(TokenType::NOT));
      ++i;
      continue;
    }

    if (c == '"')
    {
      raw.pushBack(Token(TokenType::PHRASE_START));
      ++i;
      while (i < srcLen && src[i] != '"')
      {
        if (std::isspace(static_cast<unsigned char>(src[i])))
        {
          ++i;
          continue;
        }
        size_t start = i;
        while (i < srcLen && src[i] != '"' &&
               !std::isspace(static_cast<unsigned char>(src[i])))
        {
          ++i;
        }
        raw.pushBack(Token(TokenType::WORD, SubStr(src, start, i - start)));
      }
      if (i < srcLen && src[i] == '"')
        ++i;
      raw.pushBack(Token(TokenType::PHRASE_END));
      continue;
    }

    if (IsWordChar(c))
    {
      size_t start = i;
      while (i < srcLen && IsWordChar(src[i]))
        ++i;
      raw.pushBack(Token(TokenType::WORD, SubStr(src, start, i - start)));
      continue;
    }

    ++i;
  }

  // Insert implicit AND tokens, ignoring inside phrases
  bool inPhrase = false;
  for (size_t j = 0; j < raw.size(); ++j)
  {
    if (raw[j].type == TokenType::PHRASE_START)
      inPhrase = true;
    else if (raw[j].type == TokenType::PHRASE_END)
      inPhrase = false;

    if (!inPhrase && tokens.size() > 0 &&
        NeedsImplicitAnd(tokens.back().type, raw[j].type))
    {
      tokens.pushBack(Token(TokenType::AND));
    }
    tokens.pushBack(raw[j]);
  }

  tokens.pushBack(Token(TokenType::EOF_TOKEN));
}

const Token& QueryLexer::Peek() const
{
  return tokens[pos];
}

Token QueryLexer::Consume()
{
  return tokens[pos++];
}

bool QueryLexer::Match(TokenType t)
{
  if (pos < tokens.size() && tokens[pos].type == t)
  {
    ++pos;
    return true;
  }
  return false;
}

bool QueryLexer::AllConsumed() const
{
  return pos >= tokens.size() || tokens[pos].type == TokenType::EOF_TOKEN;
}

const vector<Token>& QueryLexer::GetTokens() const
{
  return tokens;
}

const char* TokenTypeName(TokenType t)
{
  switch (t)
  {
  case TokenType::WORD:
    return "WORD";
  case TokenType::AND:
    return "AND";
  case TokenType::OR:
    return "OR";
  case TokenType::NOT:
    return "NOT";
  case TokenType::LPAREN:
    return "LPAREN";
  case TokenType::RPAREN:
    return "RPAREN";
  case TokenType::PHRASE_START:
    return "PHRASE_START";
  case TokenType::PHRASE_END:
    return "PHRASE_END";
  case TokenType::EOF_TOKEN:
    return "EOF";
  }
  return "UNKNOWN";
}

} // namespace avengine
