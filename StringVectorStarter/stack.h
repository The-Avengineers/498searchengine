// stack.h
//
// Stack built on top of vector<T>

#pragma once

#include "vector.h"

template <typename T> class Stack {
public:
  // Default constructor
  // EFFECTS: Constructs an empty stack
  Stack() : data() {}

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: pushes x onto the top of the stack
  void push(const T &x) { data.pushBack(x); }

  // REQUIRES: !empty()
  // MODIFIES: *this
  // EFFECTS: removes the top element from the stack
  void pop() {
    if (!empty()) {
      data.popBack();
    }
  }

  // REQUIRES:
  // MODIFIES:
  // EFFECTS:

  // REQUIRES:
  // MODIFIES:
  // EFFECTS:

  // REQUIRES:
  // MODIFIES:
  // EFFECTS:

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: returns true if the stack has no elements
  bool empty() const { return data.size() == 0; }

  void clear() {}

  size_t size() const {}

private:
  vector<T> data;
};
