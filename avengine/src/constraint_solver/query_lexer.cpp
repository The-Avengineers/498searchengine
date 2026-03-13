#include <avengine/constraint_solver/query_lexer.h>

#include <cctype>

namespace avengine
{
namespace constraint_solver
{
namespace
{
char lowerAscii(char c)
{
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool isWhitespace(char c)
{
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isWordChar(char c)
{
  return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

size_t skipWhitespace(const string& input, size_t offset)
{
  while (offset < input.size() && isWhitespace(input[offset]))
  {
    ++offset;
  }
  return offset;
}

struct LexStep
{
  QueryToken Token;
  size_t NextOffset{0};
};

LexStep lexFrom(const string& input, size_t offset) 
// looks at the input at one offset 
// and returns the next token plus the next offset to continue from.
{
  offset = skipWhitespace(input, offset);

  if (offset >= input.size())
  {
    return {{QueryTokenKind::End, string(), offset}, offset};
  }

  const char c = input[offset];

  switch (c)
  {
  case '|':
    return {{QueryTokenKind::Or, string("|"), offset}, offset + 1};
  case '-':
    return {{QueryTokenKind::Minus, string("-"), offset}, offset + 1};
  case '(':
    return {{QueryTokenKind::LeftParen, string("("), offset}, offset + 1};
  case ')':
    return {{QueryTokenKind::RightParen, string(")"), offset}, offset + 1};
  case '"':
  {
    string phrase;
    size_t cursor = offset + 1;

    while (cursor < input.size() && input[cursor] != '"')
    {
      phrase.pushBack(lowerAscii(input[cursor]));
      ++cursor;
    }

    if (cursor >= input.size())
    {
      string bad("\"");
      bad += phrase;
      return {{QueryTokenKind::Invalid, bad, offset}, cursor};
    }

    return {{QueryTokenKind::Phrase, phrase, offset}, cursor + 1};
  }
  default:
    break;
  }

  if (isWordChar(c))
  {
    string word;
    size_t cursor = offset;
    while (cursor < input.size() && isWordChar(input[cursor]))
    {
      word.pushBack(lowerAscii(input[cursor]));
      ++cursor;
    }
    return {{QueryTokenKind::Word, word, offset}, cursor};
  }

  string invalid;
  invalid.pushBack(c);
  return {{QueryTokenKind::Invalid, invalid, offset}, offset + 1};
}
} // namespace

QueryLexer::QueryLexer(const string& input) : Input(input) {}

QueryToken QueryLexer::Peek() const
{
  return lexFrom(Input, Offset).Token;
}

QueryToken QueryLexer::Next()
{
  const LexStep step = lexFrom(Input, Offset);
  Offset = step.NextOffset;
  return step.Token;
}

bool QueryLexer::AllConsumed() const
{
  return skipWhitespace(Input, Offset) >= Input.size();
}

} // namespace constraint_solver
} // namespace avengine
