#include <avengine/constraint_solver/query_lexer.h>
#include <avengine/indexer/token_rules.h>

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
  return avengine::is_index_token_char(static_cast<unsigned char>(c));
}

bool isInternalDelimiter(char c)
{
  return c == '-' || c == '\'';
}

bool isIgnoredSeparator(char c)
{
  switch (c)
  {
  case '.':
  case ',':
  case ':':
  case ';':
  case '/':
    return true;
  default:
    return false;
  }
}

size_t skipIgnored(const string& input, size_t offset);

bool startsPrimary(const string& input, size_t offset)
{
  offset = skipIgnored(input, offset);
  if (offset >= input.size())
    return false;

  const char c = input[offset];
  return isWordChar(c) || c == '"' || c == '(';
}

bool minusStartsUnary(const string& input, size_t offset)
{
  if (offset >= input.size() || input[offset] != '-')
    return false;

  if (!startsPrimary(input, offset + 1))
    return false;

  if (offset == 0)
    return true;

  const char prev = input[offset - 1];
  return isWhitespace(prev) || isIgnoredSeparator(prev) || prev == '(' || prev == '|';
}

size_t skipIgnored(const string& input, size_t offset)
{
  while (offset < input.size() && (isWhitespace(input[offset]) || isIgnoredSeparator(input[offset])))
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
{
  offset = skipIgnored(input, offset);

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
    if (minusStartsUnary(input, offset))
      return {{QueryTokenKind::Minus, string("-"), offset}, offset + 1};
    break;
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
    bool sawDelimiter = false;
    while (cursor < input.size())
    {
      const char current = input[cursor];
      if (isWordChar(current))
      {
        const unsigned char u = static_cast<unsigned char>(current);
        if (avengine::is_index_alpha(u))
        {
          word.pushBack(static_cast<char>(avengine::lower_index_ascii(u)));
        }
        else
        {
          word.pushBack(current);
        }
        ++cursor;
        continue;
      }

      if (isInternalDelimiter(current) && cursor + 1 < input.size() &&
          isWordChar(input[cursor + 1]))
      {
        sawDelimiter = true;
        word.pushBack(current);
        ++cursor;
        continue;
      }

      break;
    }

    return {{sawDelimiter ? QueryTokenKind::Phrase : QueryTokenKind::Word, word, offset}, cursor};
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
  return skipIgnored(Input, Offset) >= Input.size();
}

} // namespace constraint_solver
} // namespace avengine
