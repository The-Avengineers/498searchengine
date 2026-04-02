#include <avengine/crawler/domain_filter.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace
{
void fail(const char* expr, int line)
{
  std::fprintf(stderr, "domain_filter_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)
} // namespace

int main()
{
  char domainPathTemplate[] = "/tmp/domain_filter_domains_XXXXXX";
  int domainFd = mkstemp(domainPathTemplate);
  EXPECT(domainFd >= 0);

  const char bannedDomains[] = "# comment\nhttps://pornhub.com\nhttps://onlyfans.com\n";
  EXPECT(write(domainFd, bannedDomains, sizeof(bannedDomains) - 1) ==
         static_cast<ssize_t>(sizeof(bannedDomains) - 1));
  EXPECT(close(domainFd) == 0);

  char wordPathTemplate[] = "/tmp/domain_filter_words_XXXXXX";
  int wordFd = mkstemp(wordPathTemplate);
  EXPECT(wordFd >= 0);

  const char bannedTerms[] = "# comment\nporn\nadult\n";
  EXPECT(write(wordFd, bannedTerms, sizeof(bannedTerms) - 1) ==
         static_cast<ssize_t>(sizeof(bannedTerms) - 1));
  EXPECT(close(wordFd) == 0);

  avengine::crawler::domainFilter filter(domainPathTemplate, wordPathTemplate);

  EXPECT(!filter.isBlocked("https://example.com/safe"));
  EXPECT(filter.isBlocked("https://pornhub.com"));
  EXPECT(filter.isBlocked("https://onlyfans.com"));
  EXPECT(filter.isBlocked("https://example.ru"));
  EXPECT(filter.isBlocked("https://example.cn/path"));
  EXPECT(!filter.isBlocked("https://github.io"));
  EXPECT(!filter.isBlocked("https://perplexity.ai"));
  EXPECT(!filter.isBlocked("https://bbc.co.uk"));

  avengine::string safeWord1("research");
  avengine::string safeWord2("project");
  avengine::vector<avengine::string_slice> safeWords;
  safeWords.pushBack(avengine::string_slice(safeWord1));
  safeWords.pushBack(avengine::string_slice(safeWord2));
  EXPECT(!filter.containsBlockedWords(safeWords));

  avengine::string blockedWord("porn");
  avengine::vector<avengine::string_slice> blockedWords;
  blockedWords.pushBack(avengine::string_slice(safeWord1));
  blockedWords.pushBack(avengine::string_slice(blockedWord));
  EXPECT(filter.containsBlockedWords(blockedWords));

  EXPECT(unlink(domainPathTemplate) == 0);
  EXPECT(unlink(wordPathTemplate) == 0);
  return 0;
}
