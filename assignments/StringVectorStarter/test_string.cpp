// test_string.cpp
#include <cassert>
#include <iostream>

#include "string.h"

static void test_default_ctor() {
  string s;
  assert(s.size() == 0);
  assert(s.cstr()[0] == '\0');
  assert(s.begin() == s.cstr());
  assert(s.end() == s.begin()); // empty
}

static void test_cstr_ctor_small() {
  const char *txt = "hello";
  string s(txt);

  assert(s.size() == 5);
  const char *c = s.cstr();
  for (size_t i = 0; i < s.size(); ++i) {
    assert(c[i] == txt[i]);
  }
  assert(c[s.size()] == '\0');
}

static void test_cstr_ctor_large() {
  // length 20, larger than our SSO_CAP = 15
  const char *txt = "abcdefghijklmnopqrst"; // 20 chars
  size_t len = 0;
  while (txt[len] != '\0') {
    ++len;
  }

  string s(txt);
  assert(s.size() == len);

  const char *c = s.cstr();
  for (size_t i = 0; i < s.size(); ++i) {
    assert(c[i] == txt[i]);
  }
  assert(c[s.size()] == '\0');
}

static void test_element_access_and_mutation() {
  string s("hello");
  s[0] = 'H';
  s[4] = 'O';
  assert(s.size() == 5);
  assert(s[0] == 'H');
  assert(s[1] == 'e');
  assert(s[2] == 'l');
  assert(s[3] == 'l');
  assert(s[4] == 'O');
  assert(s.cstr()[5] == '\0');
}

static void test_push_back_small_to_large_boundary() {
  string s;
  // We know SSO_CAP is 15 in our implementation
  for (int i = 0; i < 15; ++i) {
    s.pushBack('a' + (i % 26));
  }
  assert(s.size() == 15);
  for (size_t i = 0; i < s.size(); ++i) {
    assert(s[i] == static_cast<char>('a' + (i % 26)));
  }
  assert(s.cstr()[15] == '\0');

  // This push should force transition from small to heap mode
  s.pushBack('Z');
  assert(s.size() == 16);
  for (size_t i = 0; i < 15; ++i) {
    assert(s[i] == static_cast<char>('a' + (i % 26)));
  }
  assert(s[15] == 'Z');
  assert(s.cstr()[16] == '\0');
}

static void test_pop_back_small_and_large() {
  string s("hello");
  s.popBack();
  assert(s.size() == 4);
  assert(s[0] == 'h');
  assert(s[1] == 'e');
  assert(s[2] == 'l');
  assert(s[3] == 'l');
  assert(s.cstr()[4] == '\0');

  // Now grow to large and pop again
  string t("abcdefghijklmnopqrst"); // 20 chars
  size_t original = t.size();
  t.popBack();
  assert(t.size() == original - 1);
  assert(t.cstr()[t.size()] == '\0');
}

static void test_append_small_to_small() {
  string a("hi");
  string b(" there");

  a += b; // "hi there"
  assert(a.size() == 8);
  const char *c = a.cstr();
  const char *expect = "hi there";
  for (size_t i = 0; i < a.size(); ++i) {
    assert(c[i] == expect[i]);
  }
  assert(c[a.size()] == '\0');
}

static void test_append_causes_heap() {
  // 10 + 10 = 20 > 15 -> heap mode in our implementation
  string a("abcdefghij"); // 10
  string b("klmnopqrst"); // 10

  a += b; // 20 chars
  assert(a.size() == 20);
  const char *c = a.cstr();
  const char *expect = "abcdefghijklmnopqrst";
  for (size_t i = 0; i < a.size(); ++i) {
    assert(c[i] == expect[i]);
  }
  assert(c[a.size()] == '\0');
}

static void test_append_heap_to_heap() {
  string a("abcdefghijklmnopqrst"); // 20
  string b("UVWXYZ");               // 6

  a += b; // 26 chars
  assert(a.size() == 26);
  const char *c = a.cstr();
  const char *expect = "abcdefghijklmnopqrstUVWXYZ";
  for (size_t i = 0; i < a.size(); ++i) {
    assert(c[i] == expect[i]);
  }
  assert(c[a.size()] == '\0');
}

static void test_copy_ctor_small_and_large() {
  // small
  string a("cat");
  string b(a);
  assert(b.size() == a.size());
  for (size_t i = 0; i < a.size(); ++i) {
    assert(b[i] == a[i]);
  }

  // large
  string c("abcdefghijklmnopqrst"); // 20
  string d(c);
  assert(d.size() == c.size());
  for (size_t i = 0; i < c.size(); ++i) {
    assert(d[i] == c[i]);
  }

  // ensure deep copy (modifying original does not change copy)
  a[0] = 'b';
  assert(b[0] == 'c');
  c[0] = 'z';
  assert(d[0] == 'a');
}

static void test_copy_assignment_small_and_large() {
  string a("dog");
  string b("xxx");
  b = a;
  assert(b.size() == a.size());
  for (size_t i = 0; i < a.size(); ++i) {
    assert(b[i] == a[i]);
  }

  string c("abcdefghijklmnopqrst"); // large
  string d("123");
  d = c;
  assert(d.size() == c.size());
  for (size_t i = 0; i < c.size(); ++i) {
    assert(d[i] == c[i]);
  }

  // self-assignment should not break anything
  c = c;
  assert(c.size() == 20);
  const char *cstr = c.cstr();
  const char *expect = "abcdefghijklmnopqrst";
  for (size_t i = 0; i < c.size(); ++i) {
    assert(cstr[i] == expect[i]);
  }
}

static void test_move_ctor_small_and_large() {
  // small
  string a("ant");
  string b(std::move(a));
  assert(b.size() == 3);
  assert(b[0] == 'a');
  assert(b[1] == 'n');
  assert(b[2] == 't');

  // original a is in a valid but empty state per our move
  assert(a.size() == 0);
  assert(a.cstr()[0] == '\0');

  // large
  string c("abcdefghijklmnopqrst"); // 20
  string d(std::move(c));
  assert(d.size() == 20);
  const char *dstr = d.cstr();
  const char *expect = "abcdefghijklmnopqrst";
  for (size_t i = 0; i < d.size(); ++i) {
    assert(dstr[i] == expect[i]);
  }
  assert(c.size() == 0);
  assert(c.cstr()[0] == '\0');
}

static void test_move_assignment_small_and_large() {
  // small
  string a("car");
  string b("xxx");
  b = std::move(a);
  assert(b.size() == 3);
  assert(b[0] == 'c');
  assert(b[1] == 'a');
  assert(b[2] == 'r');
  assert(a.size() == 0);

  // large
  string c("abcdefghijklmnopqrst"); // 20
  string d("zzz");
  d = std::move(c);
  assert(d.size() == 20);
  const char *dstr = d.cstr();
  const char *expect = "abcdefghijklmnopqrst";
  for (size_t i = 0; i < d.size(); ++i) {
    assert(dstr[i] == expect[i]);
  }
  assert(c.size() == 0);
}

static void test_comparisons() {
  string a("apple");
  string b("apple");
  string c("apples");
  string d("banana");

  // equality
  assert(a == b);
  assert(!(a != b));

  // less / greater
  assert(a < c); // "apple" < "apples"
  assert(c > a);
  assert(a < d); // 'a' < 'b'

  // <= / >=
  assert(a <= b);
  assert(a >= b);
  assert(a <= c);
  assert(c >= a);
}

static void test_stream_output() {
  string s("hello");
  std::cout << "Stream output test (should print 'hello'): " << s << "\n";
}

int main() {
  test_default_ctor();
  test_cstr_ctor_small();
  test_cstr_ctor_large();
  test_element_access_and_mutation();
  test_push_back_small_to_large_boundary();
  test_pop_back_small_and_large();
  test_append_small_to_small();
  test_append_causes_heap();
  test_append_heap_to_heap();
  test_copy_ctor_small_and_large();
  test_copy_assignment_small_and_large();
  test_move_ctor_small_and_large();
  test_move_assignment_small_and_large();
  test_comparisons();
  test_stream_output();

  std::cout << "All string tests passed.\n";
  return 0;
}
