#include <avengine/constraint_solver/query_ast.h>
#include <avengine/constraint_solver/query_parser.h>

#include <cstdio>
#include <cstdlib>

namespace
{
using avengine::string;
using avengine::constraint_solver::ParseResult;
using avengine::constraint_solver::QueryNode;
using avengine::constraint_solver::QueryNodeKind;
using avengine::constraint_solver::QueryParser;

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

QueryNode* parseOk(const char* query)
{
  QueryParser parser{string(query)};
  ParseResult result = parser.Parse();
  EXPECT(result.Errors.size() == 0);
  EXPECT(result.Root != nullptr);
  return result.Root;
}

ParseResult parseWithErrors(const char* query)
{
  QueryParser parser{string(query)};
  return parser.Parse();
}

void expectTerm(const QueryNode* node, const char* expected)
{
  EXPECT(node != nullptr);
  EXPECT(node->Kind == QueryNodeKind::Term);
  EXPECT(node->Value == string(expected));
}

void expectPhrase(const QueryNode* node, const char* a, const char* b = nullptr,
                  const char* c = nullptr)
{
  EXPECT(node != nullptr);
  EXPECT(node->Kind == QueryNodeKind::Phrase);
  size_t expectedCount = 1;
  if (b != nullptr)
    ++expectedCount;
  if (c != nullptr)
    ++expectedCount;
  EXPECT(node->PhraseTerms.size() == expectedCount);
  EXPECT(node->PhraseTerms[0] == string(a));
  if (b != nullptr)
    EXPECT(node->PhraseTerms[1] == string(b));
  if (c != nullptr)
    EXPECT(node->PhraseTerms[2] == string(c));
}

void expectConjunctionShape(const QueryNode* node, size_t requiredCount, size_t excludedCount)
{
  EXPECT(node != nullptr);
  EXPECT(node->Kind == QueryNodeKind::Conjunction);
  EXPECT(node->RequiredChildren.size() == requiredCount);
  EXPECT(node->ExcludedChildren.size() == excludedCount);
}

void expectOrShape(const QueryNode* node, size_t childCount)
{
  EXPECT(node != nullptr);
  EXPECT(node->Kind == QueryNodeKind::Or);
  EXPECT(node->Children.size() == childCount);
}

void testSingleTermQuery()
{
  QueryNode* root = parseOk("apple");
  expectTerm(root, "apple");
}

void testSinglePhraseQuery()
{
  QueryNode* root = parseOk("\"apollo moon landing\"");
  expectPhrase(root, "apollo", "moon", "landing");
}

void testImplicitAndQuery()
{
  QueryNode* root = parseOk("apple banana");
  expectConjunctionShape(root, 2, 0);
  expectTerm(root->RequiredChildren[0], "apple");
  expectTerm(root->RequiredChildren[1], "banana");
}

void testParenthesizedImplicitAndQuery()
{
  QueryNode* root = parseOk("( apple banana )");
  expectConjunctionShape(root, 2, 0);
  expectTerm(root->RequiredChildren[0], "apple");
  expectTerm(root->RequiredChildren[1], "banana");
}

void testSlideExampleQuery()
{
  QueryNode* root = parseOk("\"apollo moon landing\" | ( apple banana )");
  expectOrShape(root, 2);
  expectPhrase(root->Children[0], "apollo", "moon", "landing");
  expectConjunctionShape(root->Children[1], 2, 0);
  expectTerm(root->Children[1]->RequiredChildren[0], "apple");
  expectTerm(root->Children[1]->RequiredChildren[1], "banana");
}

void testNestedGrouping()
{
  QueryNode* root = parseOk("(apple | banana) cherry");
  expectConjunctionShape(root, 2, 0);
  expectOrShape(root->RequiredChildren[0], 2);
  expectTerm(root->RequiredChildren[0]->Children[0], "apple");
  expectTerm(root->RequiredChildren[0]->Children[1], "banana");
  expectTerm(root->RequiredChildren[1], "cherry");
}

void testExcludedTermWithinConjunction()
{
  QueryNode* root = parseOk("brown -fox");
  expectConjunctionShape(root, 1, 1);
  expectTerm(root->RequiredChildren[0], "brown");
  expectTerm(root->ExcludedChildren[0], "fox");
}

void testMultipleExcludedTerms()
{
  QueryNode* root = parseOk("alpha beta -gamma -delta");
  expectConjunctionShape(root, 2, 2);
  expectTerm(root->RequiredChildren[0], "alpha");
  expectTerm(root->RequiredChildren[1], "beta");
  expectTerm(root->ExcludedChildren[0], "gamma");
  expectTerm(root->ExcludedChildren[1], "delta");
}

void testOrChainFlattens()
{
  QueryNode* root = parseOk("alpha | beta | gamma");
  expectOrShape(root, 3);
  expectTerm(root->Children[0], "alpha");
  expectTerm(root->Children[1], "beta");
  expectTerm(root->Children[2], "gamma");
}

void testConjunctionCanContainPhrase()
{
  QueryNode* root = parseOk("\"apollo moon landing\" apple");
  expectConjunctionShape(root, 2, 0);
  expectPhrase(root->RequiredChildren[0], "apollo", "moon", "landing");
  expectTerm(root->RequiredChildren[1], "apple");
}

void testWhitespaceTolerance()
{
  QueryNode* root = parseOk("   apple   |   (  banana   cherry  )   ");
  expectOrShape(root, 2);
  expectTerm(root->Children[0], "apple");
  expectConjunctionShape(root->Children[1], 2, 0);
  expectTerm(root->Children[1]->RequiredChildren[0], "banana");
  expectTerm(root->Children[1]->RequiredChildren[1], "cherry");
}

void testNestedParenthesesAroundSingleTerm()
{
  QueryNode* root = parseOk("((apple))");
  expectTerm(root, "apple");
}

void testExcludedPhraseWithinConjunction()
{
  QueryNode* root = parseOk("apollo -\"moon landing\"");
  expectConjunctionShape(root, 1, 1);
  expectTerm(root->RequiredChildren[0], "apollo");
  expectPhrase(root->ExcludedChildren[0], "moon", "landing");
}

void testHyphenatedWordParsesAsPhrase()
{
  QueryNode* root = parseOk("Spider-man");
  expectPhrase(root, "spider", "man");
}

void testApostropheWordParsesInsideConjunction()
{
  QueryNode* root = parseOk("Joe's Pizza");
  expectConjunctionShape(root, 2, 0);
  expectPhrase(root->RequiredChildren[0], "joe", "s");
  expectTerm(root->RequiredChildren[1], "pizza");
}

void testExcludedCompoundWordWithinConjunction()
{
  QueryNode* root = parseOk("brown -Spider-man");
  expectConjunctionShape(root, 1, 1);
  expectTerm(root->RequiredChildren[0], "brown");
  expectPhrase(root->ExcludedChildren[0], "spider", "man");
}

void testStandaloneNotRejected()
{
  ParseResult result = parseWithErrors("-fox");
  EXPECT(result.Root == nullptr || result.Errors.size() > 0);
  EXPECT(result.Errors.size() > 0);
}

void testEmptyQueryRejected()
{
  ParseResult result = parseWithErrors("");
  EXPECT(result.Errors.size() > 0);
}

void testDanglingOrRejected()
{
  ParseResult result = parseWithErrors("apple |");
  EXPECT(result.Errors.size() > 0);
}

void testRepeatedOrRejected()
{
  ParseResult result = parseWithErrors("apple || banana");
  EXPECT(result.Errors.size() > 0);
}

void testUnmatchedLeftParenRejected()
{
  ParseResult result = parseWithErrors("(apple banana");
  EXPECT(result.Errors.size() > 0);
}

void testUnmatchedRightParenRejected()
{
  ParseResult result = parseWithErrors("apple banana)");
  EXPECT(result.Errors.size() > 0);
}

void testEmptyPhraseRejected()
{
  ParseResult result = parseWithErrors("\"\"");
  EXPECT(result.Errors.size() > 0);
}

void testEmptyGroupRejected()
{
  ParseResult result = parseWithErrors("()");
  EXPECT(result.Errors.size() > 0);
}
} // namespace

int main()
{
  testSingleTermQuery();
  testSinglePhraseQuery();
  testImplicitAndQuery();
  testParenthesizedImplicitAndQuery();
  testSlideExampleQuery();
  testNestedGrouping();
  testExcludedTermWithinConjunction();
  testMultipleExcludedTerms();
  testOrChainFlattens();
  testConjunctionCanContainPhrase();
  testWhitespaceTolerance();
  testNestedParenthesesAroundSingleTerm();
  testExcludedPhraseWithinConjunction();
  testHyphenatedWordParsesAsPhrase();
  testApostropheWordParsesInsideConjunction();
  testExcludedCompoundWordWithinConjunction();
  testStandaloneNotRejected();
  testEmptyQueryRejected();
  testDanglingOrRejected();
  testRepeatedOrRejected();
  testUnmatchedLeftParenRejected();
  testUnmatchedRightParenRejected();
  testEmptyPhraseRejected();
  testEmptyGroupRejected();
  return 0;
}
