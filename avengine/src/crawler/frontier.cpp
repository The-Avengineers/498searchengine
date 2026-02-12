#include "../../include/avengine/crawler/frontier.h"

namespace avengine
{
namespace crawler
{

[[gnu::always_inline]] inline static bool compare(const string& a, const string& b)
{
  return a == b;
}

[[gnu::always_inline]] inline static uint64_t stringHash(const string& s)
{
  const uint64_t P = 1315423911ULL;
  uint64_t h = 0;

  for (unsigned char c : s)
  {
    h = h * P + c;
  }
  return h;
}

frontier::frontier()
    : seen(1000000, .0001),
      // 1M expected items, 0.01% FP rate
      domainMap(compare, stringHash, 4096)
{
}
// 4096 buckets to start with

frontier::~frontier() {}

string frontier::extractDomain(const string& url) const
{
  // skip '://'
  size_t start = 0;
  for (size_t i = 0; i + 2 < url.size(); ++i)
  {
    if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/')
    {
      start = i + 3;
      break;
    }
  }

  // find end of the host
  size_t end = start;
  while (end < url.size() && url[end] != '/' && url[end] != ':')
    ++end;

  // Build lowercase domain
  string domain;
  for (size_t i = start; i < end; ++i)
  {
    char c = url[i];
    if (c >= 'A' && c <= 'Z')
    {
      c += 32;
    }
    domain += c;
  }
  return domain;
}

size_t frontier::domainLookup(const string& domain) const
{
  auto* t = domainMap.Find(domain);
  if (t)
  {
    return t->value;
  }
  else
  { // domain doesnt exist
    return SIZE_MAX;
  }
}

size_t frontier::domainInsert(const string& domain)
{
  auto* t = domainMap.Find(domain);
  if (t)
  { // already in map
    return t->value;
  }

  size_t newIdx = perDomainQueues.size();
  hostQueue hq;
  hq.domain = domain;
  perDomainQueues.pushBack(hq);
  // set domain idx to prev # of domain queues
  domainMap.Find(domain, newIdx);
  return newIdx;
}

void frontier::pushToHeap(const string& domain, float priority)
{
  heapEntry he;
  he.domain = domain;
  he.topPriority = priority;
  schedulingHeap.push(he);
}

bool frontier::alreadySeen(const string& url) const
{
  return seen.possiblyContains(url);
}

void frontier::markSeen(const string& url)
{
  seen.insert(url);
}

void frontier::seed(const vector<string>& seedURLs)
{
  for (size_t i = 0; i < seedURLs.size(); ++i)
  {
    add(seedURLs[i], 100.0f, 0); // priority is placeholder for now
  }
}

void frontier::add(const string& url, float priority, int depth)
{
  if (alreadySeen(url))
  {
    return;
  }

  markSeen(url);

  urlEntry entry;
  entry.url = url;
  entry.domain = extractDomain(url);
  entry.priority = priority;
  entry.depth = depth;

  size_t hqIdx = domainInsert(entry.domain);
  hostQueue& hq = perDomainQueues[hqIdx];
  bool wasEmpty = false;
  if (hq.pending.size() == 0)
  {
    wasEmpty = true;
  }
  hq.pending.push(entry);
  totalURLs.fetch_add(1); // increments

  // iff host just got its first URL add to scheduling
  if (wasEmpty)
  {
    pushToHeap(entry.domain, priority);
  }
}

expected<urlEntry, error> frontier::next()
{
  while (!schedulingHeap.empty())
  {
    heapEntry top = schedulingHeap.top();
    schedulingHeap.pop();
    size_t hqIdx = domainLookup(top.domain);
    if (hqIdx == SIZE_MAX)
    {
      continue;
    }
    hostQueue& hq = perDomainQueues[hqIdx];

    // host queue empty
    if (hq.pending.size() == 0)
    {
      continue;
    }

    // Pop the front URL
    urlEntry entry = hq.pending.top();
    hq.pending.pop();
    totalURLs.fetch_add(-1);

    // If host still has URLs, re-push to heap
    if (hq.pending.size() > 0)
    {
      pushToHeap(hq.domain, hq.pending.top().priority);
    }

    return expected<urlEntry, error>::ok(entry);
  }

  return expected<urlEntry, error>::error(error{"frontier empty"});
}

expected<void, error> frontier::saveCheckpoint(const string& path) const
{
  // TODO
  return expected<void, error>::ok();
}

expected<void, error> frontier::loadCheckpoint(const string& path)
{
  // TODO
  return expected<void, error>::ok();
}

size_t frontier::size() const
{
  return totalURLs.load();
}

size_t frontier::hostCount() const
{
  return perDomainQueues.size();
}

size_t frontier::seenCount() const
{
  return seen.count();
}

} // namespace crawler
} // namespace avengine