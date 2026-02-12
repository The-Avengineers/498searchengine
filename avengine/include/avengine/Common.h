#pragma once
// Common code used by the various hashing sample applications.

// Nicole Hamilton  nham@umich.edu

#include <avengine/HashTable.h>
#include <cstdint>
#include <string>
#include <vector>

namespace avengine {

// You may define additional helper routines here and in
// Common.cpp.

// YOUR CODE HERE
// function stubs for the hash function and comparator
uint64_t hashFunc(const char *key);
bool CompareEqual(const char *a, const char *b);

// Build a HashTable of strings and numbers of occurrences, given a vector
// of strings representing the words.  You may assume the vector and the
// strings will not be changed during the lifetime of the Hash.

// You may add a default for the hash function.

// Caller is responsible for deleting the Hash.

HashTable<const char *, size_t> *
BuildHashTable(const vector<string> &words,
               uint64_t (*hash)(const char *key) = hashFunc);

// Collect words read from a file specified on the command line
// as either individual word or whole lines in a vector of
// strings.  Return the index of the last argv word consumed.

int CollectWordsIn(int argc, char **argv, vector<string> &words);

// -v (verbose) command line option.

extern bool optVerbose;
} // namespace avengine
