#include "../../include/avengine/crawler/domain_filter.h"

namespace avengine
{
namespace crawler
{

[[gnu::always_inline]] inline static bool compare(const string a, const string b)
{
  return a == b;
}

[[gnu::always_inline]] inline static uint64_t stringHash(const string s)
{
  const uint64_t P = 1315423911ULL;
  uint64_t h = 0;

  for (unsigned char c : s)
  {
    h = h * P + c;
  }
  return h;
}

domainFilter::domainFilter() : blockSet(compare, stringHash, 256)
{
  // Blocklist entries should be inserted here (already lowercased).
  // Example:
  // auto* e = blockSet.Find("example.com", false);
  // e->value = true;
}

domainFilter::~domainFilter() {}

string domainFilter::extractDomain(const string& url) const
{
  // Look for "://". If missing, treat input as starting with host.
  size_t start = 0;
  for (size_t i = 0; i + 2 < url.size(); ++i)
  {
    if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/')
    {
      start = i + 3;
      break;
    }
  }

  // Host ends before path or explicit port.
  size_t end = start;
  while (end < url.size() && url[end] != '/' && url[end] != ':')
    ++end;

  // Normalize host case so lookups are stable.
  string domain;
  for (size_t i = start; i < end; ++i)
  {
    char c = url[i];
    if (c >= 'A' && c <= 'Z')
    {
      c += 32;
    }
    char temp[2] = {c, '\0'};
    domain += string(temp);
  }
  return domain;
}

bool domainFilter::isBlocked(const string& url) const
{
  // Filtering is host-based (no subdomain wildcard logic yet).
  string domain = extractDomain(url);
  auto* t = blockSet.Find(domain);
  if (t)
  {
    return true;
  }
  return false;
}

} // namespace crawler
} // namespace avengine
