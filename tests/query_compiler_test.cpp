/*
 * query_compiler_test.cpp
 *
 * Tests for the full CompileQuery pipeline (lexer + parser + decorator).
 * Verifies that field decoration expands words/phrases correctly.
 */

#include <avengine/query_compiler/ParseNode.h>
#include <avengine/query_compiler/QueryCompiler.h>

#include <cstdio>
#include <cstdlib>

namespace
{

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "query_compiler_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

using namespace avengine;

// Single word becomes OrNode of 4 decorated nodes
void testSingleWordDecoration()
{
  ParseNode* root = CompileQuery("apple");
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 4);

  auto* body = dynamic_cast<WordNode*>(orNode->children[0]);
  auto* title = dynamic_cast<WordNode*>(orNode->children[1]);
  auto* anchor = dynamic_cast<WordNode*>(orNode->children[2]);
  auto* url = dynamic_cast<WordNode*>(orNode->children[3]);

  EXPECT(body != nullptr && body->term == "apple");
  EXPECT(title != nullptr && title->term == "#apple");
  EXPECT(anchor != nullptr && anchor->term == "@apple");
  EXPECT(url != nullptr && url->term == "/apple");

  delete root;
}

// Phrase becomes OrNode of 4 PhraseNodes
void testPhraseDecoration()
{
  ParseNode* root = CompileQuery("\"quick brown fox\"");
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 4);

  auto* bodyPhrase = dynamic_cast<PhraseNode*>(orNode->children[0]);
  EXPECT(bodyPhrase != nullptr);
  EXPECT(bodyPhrase->words.size() == 3);
  EXPECT(bodyPhrase->words[0]->term == "quick");
  EXPECT(bodyPhrase->words[1]->term == "brown");
  EXPECT(bodyPhrase->words[2]->term == "fox");

  auto* titlePhrase = dynamic_cast<PhraseNode*>(orNode->children[1]);
  EXPECT(titlePhrase != nullptr);
  EXPECT(titlePhrase->words[0]->term == "#quick");
  EXPECT(titlePhrase->words[1]->term == "#brown");
  EXPECT(titlePhrase->words[2]->term == "#fox");

  auto* anchorPhrase = dynamic_cast<PhraseNode*>(orNode->children[2]);
  EXPECT(anchorPhrase != nullptr);
  EXPECT(anchorPhrase->words[0]->term == "@quick");

  auto* urlPhrase = dynamic_cast<PhraseNode*>(orNode->children[3]);
  EXPECT(urlPhrase != nullptr);
  EXPECT(urlPhrase->words[0]->term == "/quick");

  delete root;
}

//  Two words AND
void testTwoWordDecoration()
{
  ParseNode* root = CompileQuery("apple banana");
  EXPECT(root != nullptr);

  auto* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  auto* or1 = dynamic_cast<OrNode*>(andNode->children[0]);
  auto* or2 = dynamic_cast<OrNode*>(andNode->children[1]);
  EXPECT(or1 != nullptr && or1->children.size() == 4);
  EXPECT(or2 != nullptr && or2->children.size() == 4);

  auto* appleBody = dynamic_cast<WordNode*>(or1->children[0]);
  EXPECT(appleBody != nullptr && appleBody->term == "apple");

  auto* bananaTitle = dynamic_cast<WordNode*>(or2->children[1]);
  EXPECT(bananaTitle != nullptr && bananaTitle->term == "#banana");

  delete root;
}

// decorating NOT exclusion
void testNotDecoration()
{
  ParseNode* root = CompileQuery("apple -banana");
  EXPECT(root != nullptr);

  auto* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 1);
  EXPECT(andNode->excluded.size() == 1);

  auto* includedOr = dynamic_cast<OrNode*>(andNode->children[0]);
  EXPECT(includedOr != nullptr && includedOr->children.size() == 4);

  auto* excludedOr = dynamic_cast<OrNode*>(andNode->excluded[0]);
  EXPECT(excludedOr != nullptr && excludedOr->children.size() == 4);

  auto* bananaBody = dynamic_cast<WordNode*>(excludedOr->children[0]);
  EXPECT(bananaBody != nullptr && bananaBody->term == "banana");

  delete root;
}

// test Null or empty input 
void testNullInput()
{
  EXPECT(CompileQuery(nullptr) == nullptr);
  EXPECT(CompileQuery("") == nullptr);
}

// solo NOT rejected
void testSoloNotRejected()
{
  EXPECT(CompileQuery("-banana") == nullptr);
}

// testing a complex query
void testComplexDecoration()
{
  // "apollo moon landing" | ( apple -banana )
  ParseNode* root = CompileQuery("\"apollo moon landing\" | ( apple -banana )");
  EXPECT(root != nullptr);

  auto* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 2);

  // Left child OrNode wrapping 4 decorated PhraseNodes
  auto* phraseOr = dynamic_cast<OrNode*>(orNode->children[0]);
  EXPECT(phraseOr != nullptr);
  EXPECT(phraseOr->children.size() == 4);

  auto* bodyPhrase = dynamic_cast<PhraseNode*>(phraseOr->children[0]);
  EXPECT(bodyPhrase != nullptr);
  EXPECT(bodyPhrase->words.size() == 3);
  EXPECT(bodyPhrase->words[0]->term == "apollo");

  // Right child AndNode with exclusion
  auto* andNode = dynamic_cast<AndNode*>(orNode->children[1]);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 1);
  EXPECT(andNode->excluded.size() == 1);

  delete root;
}

} // namespace

int main()
{
  testSingleWordDecoration();
  testPhraseDecoration();
  testTwoWordDecoration();
  testNotDecoration();
  testNullInput();
  testStandaloneNotRejected();
  testComplexDecoration();

  std::printf("All query_compiler_test tests passed.\n");
  return 0;
}
