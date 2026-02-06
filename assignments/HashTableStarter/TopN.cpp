// TopN.cpp
// Nicole Hamilton nham@umich.edu

// Given a hashtable, return a dynamically-allocated array
// of pointers to the top N pairs based on their values.
// Caller is responsible for deleting the array.

// Individual pairs are to be C-strings as keys and size_t's
// as values.

#include "TopN.h"

#include <algorithm>

#include "HashTable.h"
using namespace std;

using Hash = HashTable<const char *, size_t>;
using Pair = Tuple<const char *, size_t>;

Pair **TopN(Hash *hashtable, int N) {
  // Find the top N pairs based on the values and return
  // as a dynamically-allocated array of pointers.  If there
  // are less than N pairs in the hash, remaining pointers
  // will be null.

  // initialize the output array with nullptrs
  Pair **top = new Pair *[N];
  for (size_t i = 0; i < N; ++i) {
    top[i] = nullptr;
  }

  // start to iterate through the hashtable, filling each empty spot in the
  // array with the first N key, val pairs
  Hash::Iterator it = hashtable->begin();
  size_t currAmt = 0;
  while (it != hashtable->end() && currAmt < N) {
    top[currAmt] = &(*it);
    it++;
    currAmt++;
  }

  // find the minimum value of the current pairs in the array (for knowing which
  // to remove and at what val >= min)
  size_t minIdx = 0;
  size_t minVal = top[0]->value;
  for (size_t i = 0; i < N; ++i) {
    if (minVal > top[i]->value) {
      minVal = top[i]->value;
      minIdx = i;
    }
  }

  // iterate through the rest of the hash table
  while (it != hashtable->end()) {
    // if the current value is greater than the min, replace the min pair and
    // re-find the minimum value pair
    if (it->value > minVal) {
      top[minIdx] = &(*it);
      minIdx = 0;
      minVal = top[0]->value;
      for (size_t i = 0; i < N; ++i) {
        if (minVal > top[i]->value) {
          minVal = top[i]->value;
          minIdx = i;
        }
      }
    }
    it++;
  }

  // using selection sort here -> good for small amts ( consider using better
  // complexity sort or maybe pq approach)
  for (size_t i = 0; i < N - 1; ++i) {
    if (top[i] == nullptr) {
      break;
    }
    for (size_t j = i + 1; j < N; ++j) {
      if (top[j] == nullptr) {
        break;
      }
      if (top[j]->value > top[i]->value) {
        Pair *tmp = top[i];
        top[i] = top[j];
        top[j] = tmp;
      }
    }
  }

  return top;
}
