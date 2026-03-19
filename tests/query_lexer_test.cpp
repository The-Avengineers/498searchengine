/*
 * query_lexer_test.cpp
 *
 * Tests for constraint_solver::QueryLexer — verifies correct tokenization of
 * search queries using the streaming lexer API.
 */

#include <avengine/constraint_solver/query_lexer.h>

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

using avengine::string;
using avengine::constraint_solver::QueryLexer;
using avengine::constraint_solver::QueryToken;
using avengine::constraint_solver::QueryTokenKind;

void testSingleWord()
{
  QueryLexer lex(string("apple"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("apple"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testTwoWords()
{
  // No implicit AND token
  QueryLexer lex(string("apple banana"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("apple"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("banana"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testOrOperator()
{
  QueryLexer lex(string("apple | banana"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Or);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testMinusOperator()
{
  QueryLexer lex(string("apple -banana"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("apple"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Minus);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("banana"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testPhrase()
{
  // Phrases are returned as a single Phrase token with the inner text.
  QueryLexer lex(string("\"quick brown fox\""));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Phrase);
  EXPECT(tok.Lexeme == string("quick brown fox"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testParentheses()
{
  QueryLexer lex(string("( apple banana )"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::LeftParen);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("apple"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("banana"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::RightParen);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testComplexQuery()
{
  QueryLexer lex(string("\"apollo moon landing\" | ( apple -banana )"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Phrase);
  EXPECT(tok.Lexeme == string("apollo moon landing"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Or);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::LeftParen);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("apple"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Minus);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("banana"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::RightParen);
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testEmptyQuery()
{
  QueryLexer lex(string(""));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testWhitespaceOnly()
{
  QueryLexer lex(string("   "));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::End);
}

void testCaseFolding()
{
  // Lexer should lower case everything
  QueryLexer lex(string("Apple BANANA"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("apple"));
  tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Word);
  EXPECT(tok.Lexeme == string("banana"));
}

void testCaseFoldingInPhrase()
{
  QueryLexer lex(string("\"Hello World\""));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Phrase);
  EXPECT(tok.Lexeme == string("hello world"));
}

void testPeekDoesNotConsume()
{
  QueryLexer lex(string("apple banana"));
  QueryToken peeked = lex.Peek();
  EXPECT(peeked.Kind == QueryTokenKind::Word);
  EXPECT(peeked.Lexeme == string("apple"));

  QueryToken peeked2 = lex.Peek();
  EXPECT(peeked2.Kind == QueryTokenKind::Word);
  EXPECT(peeked2.Lexeme == string("apple"));

  QueryToken consumed = lex.Next();
  EXPECT(consumed.Lexeme == string("apple"));
  QueryToken next = lex.Next();
  EXPECT(next.Lexeme == string("banana"));
}

void testAllConsumed()
{
  QueryLexer lex(string("apple"));
  EXPECT(!lex.AllConsumed());
  lex.Next();
  EXPECT(lex.AllConsumed());
}

void testUnclosedPhraseIsInvalid()
{
  QueryLexer lex(string("\"hello world"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Kind == QueryTokenKind::Invalid);
}

void testTokenOffset()
{
  QueryLexer lex(string("apple | banana"));
  QueryToken tok = lex.Next();
  EXPECT(tok.Offset == 0);
  tok = lex.Next();
  EXPECT(tok.Offset == 6);
  tok = lex.Next();
  EXPECT(tok.Offset == 8);
}

} // namespace

int main()
{
  testSingleWord();
  testTwoWords();
  testOrOperator();
  testMinusOperator();
  testPhrase();
  testParentheses();
  testComplexQuery();
  testEmptyQuery();
  testWhitespaceOnly();
  testCaseFolding();
  testCaseFoldingInPhrase();
  testPeekDoesNotConsume();
  testAllConsumed();
  testUnclosedPhraseIsInvalid();
  testTokenOffset();

  std::printf("All query_lexer_test tests passed.\n");
  return 0;
}
