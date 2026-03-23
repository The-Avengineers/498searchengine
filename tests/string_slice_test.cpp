// string_slice_test.cpp
//
// Assert-based tests for avengine::string_slice.
// Style mirrors string_test.cpp: no test framework, just assert + main().
// Each logical group is collected in an anonymous-namespace helper so the
// test binary's output is a clean pass/fail.

#include "avengine/string.h" // pulls in both string and string_slice

#include <cassert>
#include <utility>

namespace
{
using avengine::string;
using avengine::string_slice;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Build a mutable heap-allocated char buffer from a C string literal so we
// can safely test mutable operations without touching read-only memory.
// The caller owns the returned pointer and must delete[] it.
char* heapCopy(const char* literal)
{
  size_t len = 0;
  while (literal[len] != '\0')
  {
    ++len;
  }
  char* buf = new char[len + 1];
  for (size_t i = 0; i <= len; ++i)
  {
    buf[i] = literal[i];
  }
  return buf;
}

// -------------------------------------------------------------------------
// 1. Default / empty slice behaviour
// -------------------------------------------------------------------------
void testEmpty()
{
  string_slice empty;
  assert(empty.size() == 0);
  assert(empty.empty());
  assert(empty.begin() == empty.end());

  // Operations on empty slices must not crash or invoke UB.
  assert(empty.find("x") == string_slice::npos);
  assert(empty.find("") == 0);
  assert(empty.startsWith(string_slice()));
  assert(empty.endsWith(string_slice()));
  assert(!empty.startsWith(string_slice("a")));
  assert(!empty.endsWith(string_slice("a")));

  string_slice trimmed = empty.trim();
  assert(trimmed.size() == 0);

  avengine::vector<string_slice> parts = empty.split(',');
  assert(parts.size() == 1);
  assert(parts[0].size() == 0);

  assert(empty.toString() == string(""));
}

// -------------------------------------------------------------------------
// 2. Construction from char* + len
// -------------------------------------------------------------------------
void testConstructFromPtrLen()
{
  char* buf = heapCopy("hello");
  {
    string_slice s(buf, 5);
    assert(s.size() == 5);
    assert(!s.empty());
    assert(s[0] == 'h');
    assert(s[4] == 'o');
    assert(s.toString() == string("hello"));
  }

  // Zero-length slice from a valid pointer is fine.
  string_slice zero(buf, 0);
  assert(zero.empty());

  delete[] buf;
}

// -------------------------------------------------------------------------
// 3. Construction from avengine::string (non-const)
// -------------------------------------------------------------------------
void testConstructFromString()
{
  string src("world");
  string_slice s(src);
  assert(s.size() == src.size());
  assert(s.toString() == src);

  // Slice over a long (heap-allocated) string.
  string longStr;
  for (int i = 0; i < 50; ++i)
  {
    longStr.pushBack(static_cast<char>('a' + (i % 26)));
  }
  string_slice ls(longStr);
  assert(ls.size() == 50);
  assert(ls.toString() == longStr);
}

// -------------------------------------------------------------------------
// 4. Construction from const char*
// -------------------------------------------------------------------------
void testConstructFromCStr()
{
  // We build a read-only slice and only read through it.
  string_slice s("avengine");
  assert(s.size() == 8);
  assert(s[0] == 'a');
  assert(s[7] == 'e');
  assert(s.toString() == string("avengine"));
}

// -------------------------------------------------------------------------
// 5. Substring-view constructor (string&, start, len)
// -------------------------------------------------------------------------
void testConstructSubstringView()
{
  string src("hello world");
  string_slice hello(src, 0, 5);
  string_slice world(src, 6, 5);

  assert(hello.toString() == string("hello"));
  assert(world.toString() == string("world"));

  // Zero-length sub-view
  string_slice empty(src, 3, 0);
  assert(empty.empty());
}

// -------------------------------------------------------------------------
// 6. Mutable write through operator[], visible via toString()
// -------------------------------------------------------------------------
void testMutableWrite()
{
  char* buf = heapCopy("hello");
  string_slice s(buf, 5);

  s[0] = 'H';
  s[4] = '!';
  assert(s.toString() == string("Hell!"));

  // Mutation through a sub-slice is visible in the parent slice.
  string_slice sub = s.substr(1, 3); // "ell"
  sub[0] = 'E';
  assert(s.toString() == string("HEll!"));

  // Mutation through string_slice built from avengine::string is visible
  // when reading back from the string.
  string str("abcde");
  string_slice sv(str);
  sv[2] = 'X';
  assert(str[2] == 'X');
  assert(sv.toString() == string("abXde"));

  delete[] buf;
}

// -------------------------------------------------------------------------
// 7. substr produces a correct sub-view
// -------------------------------------------------------------------------
void testSubstr()
{
  char* buf = heapCopy("abcdef");
  string_slice full(buf, 6);

  string_slice mid = full.substr(2, 3); // "cde"
  assert(mid.size() == 3);
  assert(mid.toString() == string("cde"));

  // Mutation visible in original
  mid[0] = 'C';
  assert(full.toString() == string("abCdef"));

  // Zero-length at various positions
  assert(full.substr(0, 0).empty());
  assert(full.substr(3, 0).empty());
  assert(full.substr(6, 0).empty());

  // Full-length sub
  string_slice all = full.substr(0, 6);
  assert(all.toString() == string("abCdef"));

  delete[] buf;
}

// -------------------------------------------------------------------------
// 8. trim, trimLeft, trimRight
// -------------------------------------------------------------------------
void testTrim()
{
  // trimLeft
  {
    char* buf = heapCopy("   hello");
    string_slice s(buf, 8);
    string_slice tl = s.trimLeft();
    assert(tl.toString() == string("hello"));
    // Original unchanged
    assert(s.toString() == string("   hello"));
    delete[] buf;
  }

  // trimRight
  {
    char* buf = heapCopy("hello   ");
    string_slice s(buf, 8);
    string_slice tr = s.trimRight();
    assert(tr.toString() == string("hello"));
    delete[] buf;
  }

  // trim (both sides)
  {
    char* buf = heapCopy("  hello  ");
    string_slice s(buf, 9);
    string_slice t = s.trim();
    assert(t.toString() == string("hello"));
    delete[] buf;
  }

  // All whitespace
  {
    char* buf = heapCopy("   ");
    string_slice s(buf, 3);
    assert(s.trim().empty());
    assert(s.trimLeft().empty());
    assert(s.trimRight().empty());
    delete[] buf;
  }

  // No whitespace
  {
    string_slice s("abc");
    assert(s.trim().toString() == string("abc"));
  }

  // Mixed whitespace characters (\t, \n)
  {
    char* buf = heapCopy("\t\nhello\n\t");
    string_slice s(buf, 9);
    assert(s.trim().toString() == string("hello"));
    delete[] buf;
  }

  // Already trimmed
  {
    string_slice s("hello");
    assert(s.trimLeft().toString() == string("hello"));
    assert(s.trimRight().toString() == string("hello"));
  }
}

// -------------------------------------------------------------------------
// 9. startsWith / endsWith
// -------------------------------------------------------------------------
void testStartsEndsWith()
{
  string_slice s("hello world");

  // startsWith
  assert(s.startsWith(string_slice("hello")));
  assert(s.startsWith(string_slice("h")));
  assert(s.startsWith(string_slice(""))); // empty prefix always matches
  assert(!s.startsWith(string_slice("world")));
  assert(!s.startsWith(string_slice("hello world!"))); // longer than s

  // endsWith
  assert(s.endsWith(string_slice("world")));
  assert(s.endsWith(string_slice("d")));
  assert(s.endsWith(string_slice(""))); // empty suffix always matches
  assert(!s.endsWith(string_slice("hello")));
  assert(!s.endsWith(string_slice("!hello world"))); // longer than s

  // Empty slice
  string_slice empty;
  assert(empty.startsWith(string_slice()));
  assert(empty.endsWith(string_slice()));
  assert(!empty.startsWith(string_slice("a")));
  assert(!empty.endsWith(string_slice("a")));

  // Exact match
  assert(s.startsWith(s));
  assert(s.endsWith(s));
}

// -------------------------------------------------------------------------
// 10. find
// -------------------------------------------------------------------------
void testFind()
{
  string_slice s("abcabc");

  // Basic hit
  assert(s.find("a") == 0);
  assert(s.find("b") == 1);
  assert(s.find("abc") == 0);

  // Second occurrence via pos
  assert(s.find("a", 1) == 3);
  assert(s.find("abc", 1) == 3);

  // Miss
  assert(s.find("xyz") == string_slice::npos);
  assert(s.find("abcd") == string_slice::npos);

  // Empty substring — always matches at pos
  assert(s.find("") == 0);
  assert(s.find("", 3) == 3);
  assert(s.find("", 6) == 6);
  assert(s.find("", 7) == string_slice::npos);

  // Find on empty slice
  string_slice empty;
  assert(empty.find("") == 0);
  assert(empty.find("a") == string_slice::npos);

  // pos beyond length
  assert(s.find("a", 10) == string_slice::npos);
}

// -------------------------------------------------------------------------
// 11. split
// -------------------------------------------------------------------------
void testSplit()
{
  // No delimiter hits → single token equal to the whole slice
  {
    string_slice s("abc");
    auto parts = s.split(',');
    assert(parts.size() == 1);
    assert(parts[0].toString() == string("abc"));
  }

  // One hit
  {
    string_slice s("a,b");
    auto parts = s.split(',');
    assert(parts.size() == 2);
    assert(parts[0].toString() == string("a"));
    assert(parts[1].toString() == string("b"));
  }

  // Multiple tokens
  {
    string_slice s("one,two,three");
    auto parts = s.split(',');
    assert(parts.size() == 3);
    assert(parts[0].toString() == string("one"));
    assert(parts[1].toString() == string("two"));
    assert(parts[2].toString() == string("three"));
  }

  // Consecutive delimiters → empty token in the middle
  {
    string_slice s("a,,b");
    auto parts = s.split(',');
    assert(parts.size() == 3);
    assert(parts[0].toString() == string("a"));
    assert(parts[1].empty());
    assert(parts[2].toString() == string("b"));
  }

  // Delimiter at the start → empty leading token
  {
    string_slice s(",a,b");
    auto parts = s.split(',');
    assert(parts.size() == 3);
    assert(parts[0].empty());
    assert(parts[1].toString() == string("a"));
    assert(parts[2].toString() == string("b"));
  }

  // Delimiter at the end → empty trailing token
  {
    string_slice s("a,b,");
    auto parts = s.split(',');
    assert(parts.size() == 3);
    assert(parts[0].toString() == string("a"));
    assert(parts[1].toString() == string("b"));
    assert(parts[2].empty());
  }

  // Splitting an empty slice gives one empty token
  {
    string_slice s;
    auto parts = s.split(',');
    assert(parts.size() == 1);
    assert(parts[0].empty());
  }

  // All-delimiter string
  {
    string_slice s(",,,");
    auto parts = s.split(',');
    assert(parts.size() == 4);
    for (size_t i = 0; i < 4; ++i)
    {
      assert(parts[i].empty());
    }
  }
}

// -------------------------------------------------------------------------
// 12. All six comparison operators
// -------------------------------------------------------------------------
void testComparisons()
{
  string_slice a("apple");
  string_slice b("banana");
  string_slice a2("apple");

  // Equal pair
  assert(a == a2);
  assert(!(a != a2));
  assert(!(a < a2));
  assert(!(a > a2));
  assert(a <= a2);
  assert(a >= a2);

  // Lesser pair (a < b)
  assert(a < b);
  assert(!(b < a));
  assert(a <= b);
  assert(!(b <= a));
  assert(b > a);
  assert(!(a > b));
  assert(b >= a);
  assert(!(a >= b));
  assert(a != b);
  assert(!(a == b));

  // Prefix is less than full string
  string_slice prefix("app");
  assert(prefix < a);
  assert(a > prefix);
  assert(prefix <= a);
  assert(a >= prefix);

  // Empty slice is less than any non-empty slice
  string_slice empty;
  assert(empty < a);
  assert(a > empty);
  assert(empty <= a);
  assert(!(a <= empty));
  assert(empty == string_slice());
}

// -------------------------------------------------------------------------
// 13. toString round-trip
// -------------------------------------------------------------------------
void testToStringRoundTrip()
{
  // Round-trip for various lengths
  const char* cases[] = {"", "a", "hello", "hello world", "abcdefghijklmnopqrstuvwxyz"};
  for (const char* c : cases)
  {
    string_slice s(c);
    string result = s.toString();
    assert(result == string(c));
  }

  // Round-trip from an avengine::string — result must equal original
  string original("the quick brown fox");
  string_slice sv(original);
  string copy = sv.toString();
  assert(copy == original);
}

} // anonymous namespace

int main()
{
  testEmpty();
  testConstructFromPtrLen();
  testConstructFromString();
  testConstructFromCStr();
  testConstructSubstringView();
  testMutableWrite();
  testSubstr();
  testTrim();
  testStartsEndsWith();
  testFind();
  testSplit();
  testComparisons();
  testToStringRoundTrip();

  return 0;
}
