// test_vector.cpp
#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

#include "vector.h"

static void test_default_ctor() {
  vector<int> v;
  assert(v.size() == 0);
  assert(v.capacity() == 0);
  assert(v.begin() == v.end());
}

static void test_size_ctor() {
  vector<int> v(5);
  assert(v.size() == 5);
  assert(v.capacity() == 5);

  for (size_t i = 0; i < v.size(); ++i) {
    v[i] = static_cast<int>(i * 10);
  }
  for (size_t i = 0; i < v.size(); ++i) {
    assert(v[i] == static_cast<int>(i * 10));
  }
}

static void test_fill_ctor() {
  vector<std::string> v(4, std::string("hi"));
  assert(v.size() == 4);
  assert(v.capacity() == 4);

  for (size_t i = 0; i < v.size(); ++i) {
    assert(v[i] == "hi");
  }
}

static void test_reserve_preserves_contents() {
  vector<int> v;
  for (int i = 0; i < 10; ++i) {
    v.pushBack(i);
  }
  assert(v.size() == 10);

  v.reserve(100);
  assert(v.capacity() >= 100);
  assert(v.size() == 10);

  for (int i = 0; i < 10; ++i) {
    assert(v[static_cast<size_t>(i)] == i);
  }
}

static void test_push_pop_and_growth() {
  vector<int> v;
  size_t lastCap = v.capacity();

  for (int i = 0; i < 200; ++i) {
    v.pushBack(i);
    assert(v.size() == static_cast<size_t>(i + 1));
    assert(v[static_cast<size_t>(i)] == i);
    assert(v.capacity() >= v.size());

    if (v.capacity() != lastCap) {
      // capacity should only grow (or be 0 initially)
      assert(v.capacity() > lastCap || lastCap == 0);
      lastCap = v.capacity();
    }
  }

  for (int i = 0; i < 200; ++i) {
    assert(v[static_cast<size_t>(199 - i)] == 199 - i);
    v.popBack();
    assert(v.size() == static_cast<size_t>(199 - i));
  }

  assert(v.size() == 0);
  // capacity stays whatever it was; we just know it never went negative :)
}

static void test_iterators_and_range_for() {
  vector<int> v;
  for (int i = 1; i <= 5; ++i) {
    v.pushBack(i); // v = {1,2,3,4,5}
  }

  int sum1 = 0;
  for (int *it = v.begin(); it != v.end(); ++it) {
    sum1 += *it;
  }
  assert(sum1 == 15);

  int sum2 = 0;
  for (int x : v) { // range-for uses begin()/end()
    sum2 += x;
  }
  assert(sum2 == 15);

  const vector<int> &cv = v;
  int sum3 = 0;
  for (const int *it = cv.begin(); it != cv.end(); ++it) {
    sum3 += *it;
  }
  assert(sum3 == 15);
}

static void test_copy_ctor_deep_copy() {
  vector<int> a;
  for (int i = 0; i < 50; ++i) {
    a.pushBack(i * 2);
  }

  vector<int> b(a);
  assert(b.size() == a.size());
  assert(b.capacity() ==
         b.size()); // in our impl, capacity == size after copy ctor

  for (size_t i = 0; i < a.size(); ++i) {
    assert(b[i] == a[i]);
  }

  // mutate original, copy should not change
  a[0] = 999;
  assert(b[0] != a[0]);
}

static void test_copy_assignment_other() {
  vector<std::string> a(3, std::string("a"));
  vector<std::string> b(2, std::string("b"));

  b = a; // copy assignment
  assert(b.size() == a.size());
  for (size_t i = 0; i < b.size(); ++i) {
    assert(b[i] == "a");
  }
}

static void test_move_ctor_and_move_assignment() {
  vector<int> a;
  for (int i = 0; i < 20; ++i) {
    a.pushBack(i);
  }

  // move constructor
  vector<int> b(std::move(a));
  assert(b.size() == 20);
  for (int i = 0; i < 20; ++i) {
    assert(b[static_cast<size_t>(i)] == i);
  }
  assert(a.size() == 0);
  // capacity of moved-from 'a' is 0 in our impl

  // move assignment
  vector<int> c;
  c.pushBack(123);
  c.pushBack(456);

  c = std::move(b);
  assert(c.size() == 20);
  for (int i = 0; i < 20; ++i) {
    assert(c[static_cast<size_t>(i)] == i);
  }
  assert(b.size() == 0);
}

static void test_non_trivial_types() {
  vector<std::string> v;
  v.pushBack("hello");
  v.pushBack("world");
  v.pushBack(std::string("!!!"));

  assert(v.size() == 3);
  assert(v[0] == "hello");
  assert(v[1] == "world");
  assert(v[2] == "!!!");

  v.popBack();
  assert(v.size() == 2);
  assert(v[1] == "world");
}

static void test_stress_many_reserves_and_copies() {
  vector<int> v;
  v.reserve(1);
  for (int i = 0; i < 1000; ++i) {
    v.pushBack(i);
  }

  for (int rep = 0; rep < 10; ++rep) {
    vector<int> cpy(v);
    assert(cpy.size() == v.size());
    for (size_t i = 0; i < v.size(); ++i) {
      assert(cpy[i] == v[i]);
    }
    v.reserve(v.capacity() + 100);
    assert(v.size() == 1000);
  }
}

int main() {
  test_default_ctor();
  test_size_ctor();
  test_fill_ctor();
  test_reserve_preserves_contents();
  test_push_pop_and_growth();
  test_iterators_and_range_for();
  test_copy_ctor_deep_copy();
  test_copy_assignment_other();
  test_move_ctor_and_move_assignment();
  test_non_trivial_types();
  test_stress_many_reserves_and_copies();

  std::cout << "All vector tests passed.\n";
  return 0;
}
