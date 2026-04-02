#include "../../include/avengine/crawler/domain_filter.h"
#include <fstream>

namespace avengine
{
namespace crawler
{

namespace
{

char toLower(char c)
{
  if (c >= 'A' && c <= 'Z')
  {
    return static_cast<char>(c + 32);
  }
  return c;
}

string normalizeBlocklistLine(const char* raw)
{
  if (raw[0] == '\0' || raw[0] == '#')
  {
    return string();
  }

  string normalized;
  for (size_t i = 0; raw[i] != '\0'; ++i)
  {
    normalized.pushBack(toLower(raw[i]));
  }
  return normalized;
}

string lowerSlice(const string_slice& word)
{
  string normalized;
  for (size_t i = 0; i < word.size(); ++i)
  {
    normalized.pushBack(toLower(word[i]));
  }
  return normalized;
}

bool endsWith(const string& value, const char* suffix)
{
  size_t i = 0;
  while (suffix[i] != '\0')
  {
    ++i;
  }

  if (value.size() < i)
  {
    return false;
  }

  const size_t start = value.size() - i;
  for (size_t j = 0; j < i; ++j)
  {
    if (value[start + j] != suffix[j])
    {
      return false;
    }
  }
  return true;
}

bool hasBlockedTld(const string& domain)
{
  return endsWith(domain, ".ru") || endsWith(domain, ".su") ||  
         endsWith(domain, ".cn") || endsWith(domain, ".jp") || 
         endsWith(domain, ".kr") || endsWith(domain, ".kp");
}

} // namespace

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

domainFilter::domainFilter() : blockSet(compare, stringHash, 256), wordSet(compare, stringHash, 256)
{
  loadDomainBlockSet("banned_urls.txt");
  loadWordBlockSet("banned_words.txt");
}

domainFilter::domainFilter(const char* domainBlocklistPath, const char* wordBlocklistPath)
    : blockSet(compare, stringHash, 256), wordSet(compare, stringHash, 256)
{
  loadDomainBlockSet(domainBlocklistPath);
  loadWordBlockSet(wordBlocklistPath);
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
  string domain = extractDomain(url);
  return blockSet.Find(domain) != nullptr || hasBlockedTld(domain);
}

bool domainFilter::containsBlockedWords(const vector<string_slice>& words) const
{
  for (size_t i = 0; i < words.size(); ++i)
  {
    string normalized = lowerSlice(words[i]);
    if (wordSet.Find(normalized) != nullptr)
    {
      return true;
    }
  }
  return false;
}

void domainFilter::loadDomainBlockSet(const char* path)
{
  std::ifstream in(path);
  if (!in.is_open())
  {
    return;
  }

  char line[2048];
  while (in.getline(line, sizeof(line)))
  {
    string entry = normalizeBlocklistLine(line);
    if (entry.size() == 0)
    {
      continue;
    }

    string domain = extractDomain(entry);
    if (domain.size() == 0)
    {
      continue;
    }

    auto* blocked = blockSet.Find(domain, false);
    blocked->value = true;
  }
}

void domainFilter::loadWordBlockSet(const char* path)
{
  std::ifstream in(path);
  if (!in.is_open())
  {
    return;
  }

  char line[2048];
  while (in.getline(line, sizeof(line)))
  {
    string entry = normalizeBlocklistLine(line);
    if (entry.size() == 0)
    {
      continue;
    }

    auto* blocked = wordSet.Find(entry, false);
    blocked->value = true;
  }
}

} // namespace crawler
} // namespace avengine
