#pragma once

#include "../avengine_lib/BloomFilter.h"
#include "../avengine_lib/atomic.h"
#include "../avengine_lib/expected.h"
#include "../avengine_lib/string.h"
#include "../avengine_lib/vector.h"
#include "crawler_structs.h"

namespace avengine
{
namespace crawler
{

struct FullURLEntry
{
  urlEntry entry;
  string anchorText;
};

class frontier
{
public:
  frontier();

  ~frontier();

  // seed list
  void seed(const vector<string>& seedURLs);

  // Requires Nothing
  // Modifyies the master list by grabbing
  // 100 pseudorandom URLs from master list, sort by depth,
  // return the top 10. Remaining go back.
  vector<urlEntry> nextBatch(size_t sampleSize = 100, size_t batchSize = 10);

  // Requires a url and depth
  // Modifys frontier by adding new URL
  // Checks for duplicate URLS
  // Returns nothing
  void add(const string& url, int depth);

  // Requires a URL
  // Modifys nothing
  // Returns true if the URL has been seen
  bool alreadySeen(const string& url) const;

  // Requires a URL
  // Modifys the seen bloom filter by marking a URL as seen after success
  // Returns nothing
  void markSeen(const string& url);

  // checkpointing for crashing
  expected<void, error> saveCheckpoint(const string& path) const;
  expected<void, error> loadCheckpoint(const string& path);

  // Pop up to sampleSize random entries out of the frontier.
  // Caller handles sorting/selection policy and later appends leftovers.
  vector<urlEntry> takeRandomSample(size_t sampleSize);

  // Appends entries into the frontier master list.
  // If startIndex is provided, appends entries[startIndex..end).
  void appendBatch(const vector<urlEntry>& entries, size_t startIndex = 0);

  // stats

  // total number of URLS in master list
  size_t size() const;
  // # of URLS seen
  size_t seenCount() const;

  // Requires URL
  // Modifies Nothing
  // Returns the domain of the URL
  string extractDomain(const string& url) const;

private:
  // Our quick checker for all of the URLs we visited
  Bloomfilter seen;

  // Flat list of all pending URLs
  vector<urlEntry> masterList;
};

} // namespace crawler
} // namespace avengine
