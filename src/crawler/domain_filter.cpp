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
  // set the blocklist
}

domainFilter::~domainFilter() {}

string domainFilter::extractDomain(const string& url) const
{
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
    char temp[2] = {c, '\0'};
    domain += string(temp);
  }
  return domain;
}

bool domainFilter::isBlocked(const string& url) const
{
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
