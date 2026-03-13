#include <avengine/crawler/logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
void fail(const char* expr, int line)
{
  fprintf(stderr, "crawler_logger_test failed at line %d: %s\n", line, expr);
  abort();
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
  // Write all log overloads.
  char pathTemplate[] = "/tmp/crawl_logger_XXXXXX";
  int fd = mkstemp(pathTemplate);
  EXPECT(fd >= 0);
  EXPECT(close(fd) == 0);

  // Log stuff
  avengine::crawler::CrawlLogger logger(pathTemplate);
  logger.log("https://example.com/a", "FETCH_FAIL");
  logger.log("https://example.com/b", "HTTP_STATUS", 404);
  logger.log("https://example.com/c", "OK", static_cast<size_t>(123));
  logger.log("CHECKPOINT", 7, 11, 13);
  logger.log("DONE", 9, 17);

  // open file
  FILE* fp = fopen(pathTemplate, "rb");
  EXPECT(fp != nullptr);

  char line[512];
  int lineNo = 0;
  // Ensure everything is logged correctly and in order
  while (fgets(line, sizeof(line), fp))
  {
    if (lineNo == 0)
      EXPECT(strstr(line, ",FETCH_FAIL,https://example.com/a") != nullptr);
    if (lineNo == 1)
      EXPECT(strstr(line, ",HTTP_STATUS,https://example.com/b,404") != nullptr);
    if (lineNo == 2)
      EXPECT(strstr(line, ",OK,https://example.com/c,123") != nullptr);
    if (lineNo == 3)
      EXPECT(strstr(line, ",CHECKPOINT,7,11,13") != nullptr);
    if (lineNo == 4)
      EXPECT(strstr(line, ",DONE,9,17") != nullptr);
    lineNo++;
  }

  // close file
  EXPECT(lineNo == 5);
  EXPECT(fclose(fp) == 0);

  // Reopen same file to verify append mode.
  avengine::crawler::CrawlLogger appendLogger(pathTemplate);
  appendLogger.log("https://example.com/d", "APPEND_OK");
  appendLogger.log("https://example.com/e", "APPEND_OK_2");

  // Verify append works
  fp = fopen(pathTemplate, "rb");
  EXPECT(fp != nullptr);
  lineNo = 0;
  while (fgets(line, sizeof(line), fp))
  {
    if (lineNo == 5)
    {
      EXPECT(strstr(line, ",APPEND_OK,https://example.com/d") != nullptr);
    }
    if (lineNo == 6)
    {
      EXPECT(strstr(line, ",APPEND_OK_2,https://example.com/e") != nullptr);
    }
    lineNo++;
  }
  EXPECT(lineNo == 7);
  EXPECT(fclose(fp) == 0);

  // Invalid path shouldn;t crash
  avengine::crawler::CrawlLogger badLogger("/this/path/does/not/exist/crawl_log.csv");
  badLogger.log("https://example.com/e", "NO_CRASH");
  badLogger.log("CHECKPOINT", 1, 2, 3);

  EXPECT(unlink(pathTemplate) == 0);

  return 0;
}
