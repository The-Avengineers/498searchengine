#pragma once

#include "../avengine_lib/HashTable.h"
#include "../avengine_lib/string.h"
#include "../avengine_lib/string_slice.h"
#include "../avengine_lib/vector.h"

namespace avengine
{
namespace crawler
{

class domainFilter
{
public:
  domainFilter(const char* domainBlocklistPath, const char* wordBlocklistPath);
  ~domainFilter();

  // Requires: a URL string
  // Modifies: nothing
  // Returns: true if the URL's domain is blocked
  bool isBlocked(const string& url) const;

  // Requires: a vector of case words
  // Modifies: nothing
  // Returns: true if any word matches the blocked words file
  bool containsBlockedWords(const vector<string_slice>& words) const;

private:
  // Domain -> blocked flag.
  // The table is queried by exact host match after lowercasing.
  hashTable<string, bool> blockSet;
  hashTable<string, bool> wordSet;

  // Requires: a URL string
  // Modifies: nothing
  // Returns: the domain portion of the URL
  string extractDomain(const string& url) const;

  void loadDomainBlockSet(const char* path);
  void loadWordBlockSet(const char* path);
};

} // namespace crawler
} // namespace avengine
