/*
 * query_lexer_test.cpp
 *
 * Tests for QueryLexer — verifies correct tokenization of search queries.
 */

#include <avengine/query_compiler/QueryLexer.h>

#include <cstdio>
#include <cstdlib>

namespace
{

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "query_lexer_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

using avengine::QueryLexer;
using avengine::Token;
using avengine::TokenType;

void testSingleWord()
{
  QueryLexer lex("apple");
  auto& tokens = lex.GetTokens();
  EXPECT(tokens.size() == 2); // WORD + EOF
  EXPECT(tokens[0].type == TokenType::WORD);
  EXPECT(tokens[0].value == "apple");
  EXPECT(tokens[1].type == TokenType::EOF_TOKEN);
}

void testTwoWordsImplicitAnd()
{
  QueryLexer lex("apple banana");
  auto& tokens = lex.GetTokens();
  // WORD AND WORD EOF
  EXPECT(tokens.size() == 4);
  EXPECT(tokens[0].type == TokenType::WORD);
  EXPECT(tokens[0].value == "apple");
  EXPECT(tokens[1].type == TokenType::AND);
  EXPECT(tokens[2].type == TokenType::WORD);
  EXPECT(tokens[2].value == "banana");
  EXPECT(tokens[3].type == TokenType::EOF_TOKEN);
}

void testOrOperator()
{
  QueryLexer lex("apple | banana");
  auto& tokens = lex.GetTokens();
  // WORD OR WORD EOF
  EXPECT(tokens.size() == 4);
  EXPECT(tokens[0].type == TokenType::WORD);
  EXPECT(tokens[1].type == TokenType::OR);
  EXPECT(tokens[2].type == TokenType::WORD);
  EXPECT(tokens[3].type == TokenType::EOF_TOKEN);
}

void testNotOperator()
{
  QueryLexer lex("apple -banana");
  auto& tokens = lex.GetTokens();
  // WORD AND NOT WORD EOF
  EXPECT(tokens.size() == 5);
  EXPECT(tokens[0].type == TokenType::WORD);
  EXPECT(tokens[1].type == TokenType::AND);
  EXPECT(tokens[2].type == TokenType::NOT);
  EXPECT(tokens[3].type == TokenType::WORD);
  EXPECT(tokens[3].value == "banana");
  EXPECT(tokens[4].type == TokenType::EOF_TOKEN);
}

void testPhrase()
{
  QueryLexer lex("\"quick brown fox\"");
  auto& tokens = lex.GetTokens();
  // PHRASE_START WORD WORD WORD PHRASE_END EOF
  EXPECT(tokens.size() == 6);
  EXPECT(tokens[0].type == TokenType::PHRASE_START);
  EXPECT(tokens[1].type == TokenType::WORD);
  EXPECT(tokens[1].value == "quick");
  EXPECT(tokens[2].type == TokenType::WORD);
  EXPECT(tokens[2].value == "brown");
  EXPECT(tokens[3].type == TokenType::WORD);
  EXPECT(tokens[3].value == "fox");
  EXPECT(tokens[4].type == TokenType::PHRASE_END);
  EXPECT(tokens[5].type == TokenType::EOF_TOKEN);
}

void testParentheses()
{
  QueryLexer lex("( apple banana )");
  auto& tokens = lex.GetTokens();
  // LPAREN WORD AND WORD RPAREN EOF
  EXPECT(tokens.size() == 6);
  EXPECT(tokens[0].type == TokenType::LPAREN);
  EXPECT(tokens[1].type == TokenType::WORD);
  EXPECT(tokens[2].type == TokenType::AND);
  EXPECT(tokens[3].type == TokenType::WORD);
  EXPECT(tokens[4].type == TokenType::RPAREN);
  EXPECT(tokens[5].type == TokenType::EOF_TOKEN);
}

void testComplexQuery()
{
  // From the readme example:
  // "apollo moon landing" | ( apple -banana )
  QueryLexer lex("\"apollo moon landing\" | ( apple -banana )");
  auto& tokens = lex.GetTokens();
  // PHRASE_START WORD WORD WORD PHRASE_END OR LPAREN WORD AND NOT WORD RPAREN EOF
  EXPECT(tokens.size() == 13);
  EXPECT(tokens[0].type == TokenType::PHRASE_START);
  EXPECT(tokens[1].value == "apollo");
  EXPECT(tokens[2].value == "moon");
  EXPECT(tokens[3].value == "landing");
  EXPECT(tokens[4].type == TokenType::PHRASE_END);
  EXPECT(tokens[5].type == TokenType::OR);
  EXPECT(tokens[6].type == TokenType::LPAREN);
  EXPECT(tokens[7].type == TokenType::WORD);
  EXPECT(tokens[7].value == "apple");
  EXPECT(tokens[8].type == TokenType::AND);
  EXPECT(tokens[9].type == TokenType::NOT);
  EXPECT(tokens[10].type == TokenType::WORD);
  EXPECT(tokens[10].value == "banana");
  EXPECT(tokens[11].type == TokenType::RPAREN);
  EXPECT(tokens[12].type == TokenType::EOF_TOKEN);
}

void testEmptyQuery()
{
  QueryLexer lex("");
  auto& tokens = lex.GetTokens();
  EXPECT(tokens.size() == 1);
  EXPECT(tokens[0].type == TokenType::EOF_TOKEN);
}

void testWhitespaceOnly()
{
  QueryLexer lex("   ");
  auto& tokens = lex.GetTokens();
  EXPECT(tokens.size() == 1);
  EXPECT(tokens[0].type == TokenType::EOF_TOKEN);
}

void testMatchAndConsume()
{
  QueryLexer lex("apple banana");
  EXPECT(lex.Peek().type == TokenType::WORD);
  EXPECT(lex.Match(TokenType::WORD));
  EXPECT(lex.Match(TokenType::AND));
  EXPECT(lex.Match(TokenType::WORD));
  EXPECT(lex.AllConsumed());
}

void testImplicitAndAfterParen()
{
  // ) followed by WORD should insert AND
  QueryLexer lex("(apple) banana");
  auto& tokens = lex.GetTokens();
  // LPAREN WORD RPAREN AND WORD EOF
  EXPECT(tokens.size() == 6);
  EXPECT(tokens[2].type == TokenType::RPAREN);
  EXPECT(tokens[3].type == TokenType::AND);
  EXPECT(tokens[4].type == TokenType::WORD);
}

void testImplicitAndAfterPhrase()
{
  // PHRASE_END followed by WORD should insert AND
  QueryLexer lex("\"hello world\" apple");
  auto& tokens = lex.GetTokens();
  // PHRASE_START WORD WORD PHRASE_END AND WORD EOF
  EXPECT(tokens.size() == 7);
  EXPECT(tokens[3].type == TokenType::PHRASE_END);
  EXPECT(tokens[4].type == TokenType::AND);
  EXPECT(tokens[5].type == TokenType::WORD);
}

} // namespace

int main()
{
  testSingleWord();
  testTwoWordsImplicitAnd();
  testOrOperator();
  testNotOperator();
  testPhrase();
  testParentheses();
  testComplexQuery();
  testEmptyQuery();
  testWhitespaceOnly();
  testMatchAndConsume();
  testImplicitAndAfterParen();
  testImplicitAndAfterPhrase();

  std::printf("All query_lexer_test tests passed.\n");
  return 0;
}
