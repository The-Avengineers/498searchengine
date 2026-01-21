// stack.h
//
// Stack built on top of vector<T>

#pragma once

#include "vector.h"

template <typename T> class Stack {
public:
  // Default constructor
  // EFFECTS: Constructs an empty stack
  Stack() : data() {
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Copy constructor - constructs a stack as a copy of other
  Stack(const Stack& other) : data(other.data) {
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Copy assignment operator - assigns other to *this
  Stack& operator=(const Stack& other) {
    if (this != &other) {
      data = other.data;
    }
    return *this;
  }

  // REQUIRES: Nothing
  // MODIFIES: *this and other
  // EFFECTS: Move constructor - constructs a stack by moving from other
  Stack(Stack&& other) : data() {
    swap(other);
  }

  // REQUIRES: Nothing
  // MODIFIES: *this and other
  // EFFECTS: Move assignment operator - moves other to *this
  Stack& operator=(Stack&& other) {
    if (this != &other) {
      swap(other);
    }
    return *this;
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: pushes x onto the top of the stack
  void push(const T &x) { 
    data.pushBack(x); 
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: removes the top element from the stack
  void pop() {
    if (!empty()) {
      data.popBack();
    }
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if the stack is empty, false otherwise
  bool empty() const {
    return data.size() == 0;
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Removes all elements from the stack
  void clear() {
    while (!empty()) {
      data.popBack();
    }
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the number of elements in the stack
  size_t size() const {
    return data.size();
  }

  // REQUIRES: !empty()
  // MODIFIES: Nothing
  // EFFECTS: Returns a reference to the top element of the stack
  T& top() {
    return data[data.size() - 1];
  }

  // REQUIRES: !empty()
  // MODIFIES: Nothing
  // EFFECTS: Returns a const reference to the top element of the stack
  const T& top() const {
    return data[data.size() - 1];
  }

  // REQUIRES: Nothing
  // MODIFIES: *this and other
  // EFFECTS: Swaps the contents of *this and other
  void swap(Stack& other) {
    vector<T> tmp = data;
    data = other.data;
    other.data = tmp;
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if *this and other contain the same elements
  //          in the same order, false otherwise
  bool operator==(const Stack& other) const {
    if(this->size() != other.size()){
      return false;
    }

    return this->data == other.data;

  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if *this and other are not equal, false otherwise
  bool operator!=(const Stack& other) const {
    return !(*this == other);
  }

private:
  vector<T> data;

};
