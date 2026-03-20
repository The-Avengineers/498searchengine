#include <avengine/constraint_solver/constraint_solver.h>

#include <cstdio>
#include <cstdlib>

namespace
{
using avengine::Dictionary;
using avengine::ISREndDoc;
using avengine::ISRWord;
using avengine::string;
using avengine::vector;
using avengine::constraint_solver::ConstraintSolver;
using avengine::constraint_solver::DestroyQueryTree;
using avengine::constraint_solver::DocumentMatch;
using avengine::constraint_solver::IsrSource;
using avengine::constraint_solver::ParseResult;

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "constraint_solver_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

class DictionarySource : public IsrSource
{
public:
  void AddDocument(const char* name, size_t endLocation)
  {
    names_.pushBack(string(name));
    dict_.AddDocument(endLocation);
  }

  void AddOccurrence(const char* term, size_t location)
  {
    dict_.AddOccurrence(term, location);
  }

  ISRWord* OpenTermIsr(const string& term) const override
  {
    return dict_.OpenIsr(term.cstr());
  }

  ISREndDoc* OpenEndDocIsr() const override
  {
    return dict_.OpenEndDocIsr();
  }

  string GetDocumentName(uint32_t docId) const override
  {
    if (docId >= names_.size())
      return string();
    return names_[docId];
  }

private:
  Dictionary dict_;
  vector<string> names_;
};

void expectMatches(const vector<DocumentMatch>& matches, std::initializer_list<const char*> names)
{
  EXPECT(matches.size() == names.size());

  size_t index = 0;
  for (const char* name : names)
  {
    EXPECT(matches[index].Url == string(name));
    ++index;
  }
}

DictionarySource makePhraseSource()
{
  DictionarySource source;
  source.AddOccurrence("apollo", 1);
  source.AddOccurrence("moon", 2);
  source.AddOccurrence("landing", 3);
  source.AddOccurrence("apple", 4);
  source.AddDocument("doc-0", 4);

  source.AddOccurrence("apollo", 5);
  source.AddDocument("doc-1", 5);

  source.AddOccurrence("moon", 6);
  source.AddOccurrence("landing", 7);
  source.AddOccurrence("apple", 8);
  source.AddDocument("doc-2", 8);
  return source;
}

DictionarySource makeBooleanSource()
{
  DictionarySource source;
  source.AddOccurrence("apple", 1);
  source.AddOccurrence("banana", 2);
  source.AddOccurrence("cherry", 3);
  source.AddDocument("doc-a", 3);

  source.AddOccurrence("brown", 4);
  source.AddOccurrence("fox", 5);
  source.AddOccurrence("apple", 6);
  source.AddDocument("doc-b", 6);

  source.AddOccurrence("brown", 7);
  source.AddOccurrence("banana", 8);
  source.AddOccurrence("cherry", 9);
  source.AddDocument("doc-c", 9);
  return source;
}

DictionarySource makeGroupedExclusionSource()
{
  DictionarySource source;
  source.AddOccurrence("apple", 1);
  source.AddOccurrence("cherry", 2);
  source.AddDocument("doc-x", 2);

  source.AddOccurrence("banana", 3);
  source.AddOccurrence("cherry", 4);
  source.AddDocument("doc-y", 4);

  source.AddOccurrence("cherry", 5);
  source.AddDocument("doc-z", 5);
  return source;
}

void testSingleTermSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-b"});
}

void testPhraseSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makePhraseSource();
  auto result = solver.SolveQuery("\"apollo moon landing\"", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-0"});
}

void testImplicitAndSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("banana cherry", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-c"});
}

void testOrSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple | brown", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-b", "doc-c"});
}

void testAndNotSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("brown -fox", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-c"});
}

void testGroupedSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("(apple | banana) cherry", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-c"});
}

void testMissingRequiredTermReturnsEmpty()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple kiwi", source);
  EXPECT(result.Errors.size() == 0);
  EXPECT(result.Matches.size() == 0);
}

void testOrWithMissingBranchStillMatches()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("kiwi | apple", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-b"});
}

void testDuplicateOrDeduplicatesMatches()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple | apple", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-b"});
}

void testDuplicateRequiredTermStaysStable()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple apple", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-b"});
}

void testPhraseExclusionSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makePhraseSource();
  auto result = solver.SolveQuery("apollo -\"moon landing\"", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-1"});
}

void testGroupedExclusionSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeGroupedExclusionSource();
  auto result = solver.SolveQuery("cherry -(apple | banana)", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-z"});
}

void testOrAndPrecedenceSolve()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple | banana cherry", source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-b", "doc-c"});
}

void testSolveParsedPath()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  ParseResult parsed = solver.ParseQuery("(apple | banana) cherry");
  EXPECT(parsed.Errors.size() == 0);
  EXPECT(parsed.Root != nullptr);

  auto result = solver.SolveParsed(parsed.Root, source);
  EXPECT(result.Errors.size() == 0);
  expectMatches(result.Matches, {"doc-a", "doc-c"});

  DestroyQueryTree(parsed.Root);
}

void testInvalidQueryErrorsPassThrough()
{
  ConstraintSolver solver;
  DictionarySource source = makeBooleanSource();
  auto result = solver.SolveQuery("apple |", source);
  EXPECT(result.Errors.size() > 0);
  EXPECT(result.Matches.size() == 0);
}

void testEmptySourceReturnsNoMatches()
{
  ConstraintSolver solver;
  DictionarySource source;
  auto result = solver.SolveQuery("apple", source);
  EXPECT(result.Errors.size() == 0);
  EXPECT(result.Matches.size() == 0);
}

} // namespace

int main()
{
  testSingleTermSolve();
  testPhraseSolve();
  testImplicitAndSolve();
  testOrSolve();
  testAndNotSolve();
  testGroupedSolve();
  testMissingRequiredTermReturnsEmpty();
  testOrWithMissingBranchStillMatches();
  testDuplicateOrDeduplicatesMatches();
  testDuplicateRequiredTermStaysStable();
  testPhraseExclusionSolve();
  testGroupedExclusionSolve();
  testOrAndPrecedenceSolve();
  testSolveParsedPath();
  testInvalidQueryErrorsPassThrough();
  testEmptySourceReturnsNoMatches();
  return 0;
}
