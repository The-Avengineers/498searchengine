#include "../avengine_lib/atomic.h"
#include "../avengine_lib/bloomFilter.h"
#include "../avengine_lib/deque.h"
#include "../avengine_lib/expected.h"
#include "../avengine_lib/hashTable.h"
#include "../avengine_lib/string.h"
#include "avengine_lib/priorityQueue.h"
#include "avengine_lib/vector.h"
#include "crawler_structs.h"

namespace avengine
{
namespace crawler
{

class frontier
{
public:
  // seed list
  void seed(const vector<string>& seedURLs);

  // Requires nothing
  // Modifies nothing
  // Returns next URL to crawl or error if frontier is empty
  expected<urlEntry, error> next();

  // Requires a url, priority, depth
  // Modifys frontier by adding new URL
  // Checks for duplicate URLS
  // Returns nothing
  void add(const string& url, float priority, int depth);

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

  // stats

  // total number of URLS across all of the host Qs
  size_t size() const;
  // # of active host queues
  size_t hostCount() const;
  // # of URLS seen
  size_t seenCount() const;

private:
  // Our quick checker for all of the URLs we visited
  bloomFilter seen;

  // # of URLs we have crawled
  avengine::atomic_counter totalURLs{};

  // Create one queue per domain
  // domain ---> index mapping
  vector<hostQueue> perDomainQueues;

  // domain ---> index mapping
  hashTable<string, size_t> domainMap;

  // Requires domain
  // Modifys nothing
  // Returns the Index its at or -1 or size_t::max on failure
  size_t domainLookup(const string& domain) const;

  // Requires doamin
  // Modifys domainMap
  // Returns index of insertion
  size_t domainInsert(const string& domain);

  // Max heap based on priorities for scheduling
  priorityQueue<heapEntry, less<heapEntry>> schedulingHeap;

  // Requires domain and priority
  // Modifies the heap by building the entry and pushing it
  // Returns nothing
  void pushToHeap(const string& domain, float priority);

  // Requires URL
  // Modifies Nothing
  // Returns the domain of the URL
  string extractDomain(const string& url) const;
};

} // namespace crawler
} // namespace avengine