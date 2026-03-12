/*
 * query_parser_test.cpp
 *
 * Tests for QueryParser — verifies correct parse tree construction.
 */

#include <avengine/query_compiler/ParseNode.h>
#include <avengine/query_compiler/QueryParser.h>

#include <cstdio>
#include <cstdlib>

namespace
{

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "query_parser_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

using namespace avengine;

// single word
void testSingleWord()
{
  QueryParser parser("apple");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* word = dynamic_cast<WordNode*>(root);
  EXPECT(word != nullptr);
  EXPECT(word->term == "apple");

  delete root;
}

// two words make an AndNode 
void testImplicitAnd()
{
  QueryParser parser("apple banana");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  auto* w1 = dynamic_cast<WordNode*>(andNode->children[0]);
  auto* w2 = dynamic_cast<WordNode*>(andNode->children[1]);
  EXPECT(w1 != nullptr);
  EXPECT(w2 != nullptr);
  EXPECT(w1->term == "apple");
  EXPECT(w2->term == "banana");

  delete root;
}

// Three words make AndNode with 3 children
void testThreeWords()
{
  QueryParser parser("apple banana cherry");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 3);

  delete root;
}

// Testing Or operator
void testOr()
{
  QueryParser parser("apple | banana");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 2);

  auto* w1 = dynamic_cast<WordNode*>(orNode->children[0]);
  auto* w2 = dynamic_cast<WordNode*>(orNode->children[1]);
  EXPECT(w1 != nullptr && w1->term == "apple");
  EXPECT(w2 != nullptr && w2->term == "banana");

  delete root;
}

// NOT make an exclusion in AndNode 
void testNot()
{
  QueryParser parser("apple -banana");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 1);
  EXPECT(andNode->excluded.size() == 1);

  auto* included = dynamic_cast<WordNode*>(andNode->children[0]);
  auto* excludedWord = dynamic_cast<WordNode*>(andNode->excluded[0]);
  EXPECT(included != nullptr && included->term == "apple");
  EXPECT(excludedWord != nullptr && excludedWord->term == "banana");

  delete root;
}

// rejecting solo NOT bc not allowed
void testStandaloneNotRejected()
{
  QueryParser parser("-banana");
  ParseNode* root = parser.Parse();
  EXPECT(root == nullptr);
}

// phrase node
void testPhrase()
{
  QueryParser parser("\"quick brown fox\"");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* phrase = dynamic_cast<PhraseNode*>(root);
  EXPECT(phrase != nullptr);
  EXPECT(phrase->words.size() == 3);
  EXPECT(phrase->words[0]->term == "quick");
  EXPECT(phrase->words[1]->term == "brown");
  EXPECT(phrase->words[2]->term == "fox");

  delete root;
}

// phrase AND word
void testPhraseAndWord()
{
  QueryParser parser("\"quick brown\" fox");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  auto* phrase = dynamic_cast<PhraseNode*>(andNode->children[0]);
  auto* word = dynamic_cast<WordNode*>(andNode->children[1]);
  EXPECT(phrase != nullptr);
  EXPECT(word != nullptr);
  EXPECT(phrase->words.size() == 2);
  EXPECT(word->term == "fox");

  delete root;
}

// group by parentheses
void testGrouping()
{
  // apple | (banana cherry)
  // Should be OrNode( WordNode(apple), AndNode(banana, cherry) )
  QueryParser parser("apple | (banana cherry)");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 2);

  auto* w = dynamic_cast<WordNode*>(orNode->children[0]);
  EXPECT(w != nullptr && w->term == "apple");

  auto* andNode = dynamic_cast<AndNode*>(orNode->children[1]);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  delete root;
}

// unmatched parenthesis returns nullptr ? should this be allowed and just ignored?
void testUnmatchedParen()
{
  QueryParser parser("(apple banana");
  ParseNode* root = parser.Parse();
  EXPECT(root == nullptr);
}

// empty query returns nullptr not allowed
void testEmptyQuery()
{
  QueryParser parser("");
  ParseNode* root = parser.Parse();
  EXPECT(root == nullptr);
}

// slightly more complex query
void testComplexQuery()
{
  // "apollo moon landing" | ( apple -banana )
  QueryParser parser("\"apollo moon landing\" | ( apple -banana )");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 2);

  auto* phrase = dynamic_cast<PhraseNode*>(orNode->children[0]);
  EXPECT(phrase != nullptr);
  EXPECT(phrase->words.size() == 3);

  auto* andNode = dynamic_cast<AndNode*>(orNode->children[1]);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 1);
  EXPECT(andNode->excluded.size() == 1);

  delete root;
}

// testing or precedence
void testPrecedence()
{
  // apple banana | cherry
  // Should be: OrNode( AndNode(apple, banana), WordNode(cherry) )
  QueryParser parser("apple banana | cherry");
  ParseNode* root = parser.Parse();
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 2);

  auto* andNode = dynamic_cast<AndNode*>(orNode->children[0]);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  auto* word = dynamic_cast<WordNode*>(orNode->children[1]);
  EXPECT(word != nullptr && word->term == "cherry");

  delete root;
}

} // namespace

int main()
{
  testSingleWord();
  testImplicitAnd();
  testThreeWords();
  testOr();
  testNot();
  testStandaloneNotRejected();
  testPhrase();
  testPhraseAndWord();
  testGrouping();
  testUnmatchedParen();
  testEmptyQuery();
  testComplexQuery();
  testPrecedence();

  std::printf("All query_parser_test tests passed.\n");
  return 0;
}
