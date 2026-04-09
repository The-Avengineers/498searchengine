#include <cstdio>
#include <cstdlib>

#define main crawler_main_entrypoint
#include "../src/crawler/crawler_main.cpp"
#undef main

namespace
{
void fail(const char* expr, int line)
{
  std::fprintf(stderr, "crawler_main_github_filter_test failed at line %d: %s\n", line, expr);
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
  avengine::crawler::URLNormalizer normalizer;

  // Allowed GitHub paths: root, one segment, two segments.
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com/"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux/"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds?tab=repositories"));
  EXPECT(
      passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux?tab=readme-ov-file"));

  // Block deeper GitHub paths.
  EXPECT(!passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux/issues"));
  EXPECT(
      !passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux/blob/master/README"));
  EXPECT(!passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux/tree/master"));
  EXPECT(!passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux/commit/abc123"));
  EXPECT(!passesGitHubCrawlPolicy(normalizer, "https://github.com/torvalds/linux/pulls"));

  // Non-GitHub URLs are unaffected.
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://example.com/a/b/c/d"));
  EXPECT(passesGitHubCrawlPolicy(normalizer, "https://github.io/a/b/c"));

  return 0;
}
