/*
 * query_decorator_test.cpp
 *
 * Tests for QueryDecorator — verifies field expansion of parse tree nodes.
 * Tests the decorator in isolation by building parse trees manually
 * rather than going through the parser.
 */

#include <avengine/query_compiler/ParseNode.h>
#include <avengine/query_compiler/QueryDecorator.h>

#include <cstdio>
#include <cstdlib>

namespace
{

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "query_decorator_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

using namespace avengine;

// WordNode becomes OrNode with 4 decorators  
void testDecorateWord()
{
  ParseNode* root = QueryDecorator::Decorate(new WordNode("apple"));
  EXPECT(root != nullptr);

  OrNode* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 4);

  WordNode* body = dynamic_cast<WordNode*>(orNode->children[0]);
  WordNode* title = dynamic_cast<WordNode*>(orNode->children[1]);
  WordNode* anchor = dynamic_cast<WordNode*>(orNode->children[2]);
  WordNode* url = dynamic_cast<WordNode*>(orNode->children[3]);

  EXPECT(body != nullptr && body->term == "apple");
  EXPECT(title != nullptr && title->term == "#apple");
  EXPECT(anchor != nullptr && anchor->term == "@apple");
  EXPECT(url != nullptr && url->term == "/apple");

  delete root;
}

// PhraseNode becomes OrNode with 4 decorated PhraseNodes 
void testDecoratePhrase()
{
  vector<WordNode*> words;
  words.pushBack(new WordNode("hello"));
  words.pushBack(new WordNode("world"));

  ParseNode* root = QueryDecorator::Decorate(new PhraseNode(words));
  EXPECT(root != nullptr);

  OrNode* orNode = dynamic_cast<OrNode*>(root);
  EXPECT(orNode != nullptr);
  EXPECT(orNode->children.size() == 4);

  PhraseNode* bodyPhrase = dynamic_cast<PhraseNode*>(orNode->children[0]);
  EXPECT(bodyPhrase != nullptr);
  EXPECT(bodyPhrase->words.size() == 2);
  EXPECT(bodyPhrase->words[0]->term == "hello");
  EXPECT(bodyPhrase->words[1]->term == "world");

  PhraseNode* titlePhrase = dynamic_cast<PhraseNode*>(orNode->children[1]);
  EXPECT(titlePhrase != nullptr);
  EXPECT(titlePhrase->words[0]->term == "#hello");
  EXPECT(titlePhrase->words[1]->term == "#world");

  PhraseNode* anchorPhrase = dynamic_cast<PhraseNode*>(orNode->children[2]);
  EXPECT(anchorPhrase != nullptr);
  EXPECT(anchorPhrase->words[0]->term == "@hello");

  PhraseNode* urlPhrase = dynamic_cast<PhraseNode*>(orNode->children[3]);
  EXPECT(urlPhrase != nullptr);
  EXPECT(urlPhrase->words[0]->term == "/hello");

  delete root;
}

// AndNode children are decorated and preserve structure
void testDecorateAndNode()
{
  vector<ParseNode*> children;
  children.pushBack(new WordNode("cat"));
  children.pushBack(new WordNode("dog"));

  ParseNode* root = QueryDecorator::Decorate(new AndNode(children));
  EXPECT(root != nullptr);

  AndNode* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  // Each child should now be an OrNode with 4 variants
  OrNode* or1 = dynamic_cast<OrNode*>(andNode->children[0]);
  OrNode* or2 = dynamic_cast<OrNode*>(andNode->children[1]);
  EXPECT(or1 != nullptr && or1->children.size() == 4);
  EXPECT(or2 != nullptr && or2->children.size() == 4);

  WordNode* catBody = dynamic_cast<WordNode*>(or1->children[0]);
  WordNode* dogTitle = dynamic_cast<WordNode*>(or2->children[1]);
  EXPECT(catBody != nullptr && catBody->term == "cat");
  EXPECT(dogTitle != nullptr && dogTitle->term == "#dog");

  delete root;
}

// AndNode with exclusions are also decorated
void testDecorateAndNodeWithExclusions()
{
  vector<ParseNode*> children;
  children.pushBack(new WordNode("cat"));
  vector<ParseNode*> excluded;
  excluded.pushBack(new WordNode("rat"));

  ParseNode* root = QueryDecorator::Decorate(new AndNode(children, excluded));
  EXPECT(root != nullptr);

  AndNode* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 1);
  EXPECT(andNode->excluded.size() == 1);

  OrNode* exOr = dynamic_cast<OrNode*>(andNode->excluded[0]);
  EXPECT(exOr != nullptr && exOr->children.size() == 4);

  WordNode* ratBody = dynamic_cast<WordNode*>(exOr->children[0]);
  WordNode* ratAnchor = dynamic_cast<WordNode*>(exOr->children[2]);
  EXPECT(ratBody != nullptr && ratBody->term == "rat");
  EXPECT(ratAnchor != nullptr && ratAnchor->term == "@rat");

  delete root;
}

// OrNode children are decorated and structure preserved 
void testDecorateOrNode()
{
  vector<ParseNode*> children;
  children.pushBack(new WordNode("red"));
  children.pushBack(new WordNode("blue"));

  ParseNode* root = QueryDecorator::Decorate(new OrNode(children));
  EXPECT(root != nullptr);

  OrNode* outerOr = dynamic_cast<OrNode*>(root);
  EXPECT(outerOr != nullptr);
  EXPECT(outerOr->children.size() == 2);

  OrNode* innerOr1 = dynamic_cast<OrNode*>(outerOr->children[0]);
  OrNode* innerOr2 = dynamic_cast<OrNode*>(outerOr->children[1]);
  EXPECT(innerOr1 != nullptr && innerOr1->children.size() == 4);
  EXPECT(innerOr2 != nullptr && innerOr2->children.size() == 4);

  delete root;
}

// NotNode child is decorated
void testDecorateNotNode()
{
  ParseNode* root = QueryDecorator::Decorate(new NotNode(new WordNode("spam")));
  EXPECT(root != nullptr);

  NotNode* notNode = dynamic_cast<NotNode*>(root);
  EXPECT(notNode != nullptr);

  OrNode* innerOr = dynamic_cast<OrNode*>(notNode->child);
  EXPECT(innerOr != nullptr && innerOr->children.size() == 4);

  WordNode* spamUrl = dynamic_cast<WordNode*>(innerOr->children[3]);
  EXPECT(spamUrl != nullptr && spamUrl->term == "/spam");

  delete root;
}

// nullptr input returns nullptr
void testDecorateNull()
{
  EXPECT(QueryDecorator::Decorate(nullptr) == nullptr);
}

// Nested structure of And containing Or containing Words 
void testDecorateNestedTree()
{
  vector<ParseNode*> orChildren;
  orChildren.pushBack(new WordNode("a"));
  orChildren.pushBack(new WordNode("b"));

  vector<ParseNode*> andChildren;
  andChildren.pushBack(new OrNode(orChildren));
  andChildren.pushBack(new WordNode("c"));

  ParseNode* root = QueryDecorator::Decorate(new AndNode(andChildren));
  EXPECT(root != nullptr);

  AndNode* andNode = dynamic_cast<AndNode*>(root);
  EXPECT(andNode != nullptr);
  EXPECT(andNode->children.size() == 2);

  OrNode* outerOr = dynamic_cast<OrNode*>(andNode->children[0]);
  EXPECT(outerOr != nullptr);
  EXPECT(outerOr->children.size() == 2);

  OrNode* aOr = dynamic_cast<OrNode*>(outerOr->children[0]);
  EXPECT(aOr != nullptr && aOr->children.size() == 4);

  OrNode* cOr = dynamic_cast<OrNode*>(andNode->children[1]);
  EXPECT(cOr != nullptr && cOr->children.size() == 4);

  delete root;
}

} // namespace

int main()
{
  testDecorateWord();
  testDecoratePhrase();
  testDecorateAndNode();
  testDecorateAndNodeWithExclusions();
  testDecorateOrNode();
  testDecorateNotNode();
  testDecorateNull();
  testDecorateNestedTree();

  std::printf("All query_decorator_test tests passed.\n");
  return 0;
}
