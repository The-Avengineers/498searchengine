#pragma once

#include "../avengine_lib/string.h"
#include <ctime>
#include <fstream>

namespace avengine
{
namespace crawler
{

class CrawlLogger
{
public:
  // File path for the logger
  CrawlLogger(const char* path);
  ~CrawlLogger();

  // NOTE, ALL log overloads have the same functionality essentially

  // Requires URL and a Message
  // Modifys the logger by appending a message
  // Returns nothing
  // Used when fetch failed
  void log(const string& url, const char* msg);

  // Requires a URL, message, and an http status code
  // Modifys the logger by appending a message
  // Returns nothing
  // used for http responses
  void log(const string& url, const char* msg, int statusCode);

  // Requires a URL, message, and and the number of bytes recieved
  // Modifys the logger by appending a message
  // Returns nothing
  // Used for a successful fetch
  void log(const string& url, const char* msg, size_t bytes);

  // Requires the crawlCount
  // Modifys the logger by appending a message
  // Returns nothing
  // Used for periodic checkpointing
  void log(const char* event, size_t count, size_t seenCount, size_t listSize);

  // Requires the crawl count and seen count
  // Modifys the logger by appending a message
  // Returns nothing
  // Used for when the crawl is done
  void log(const char* event, size_t count, size_t seenCount);

private:
  std::ofstream logFile;

  // Requires. nothing
  // Modifys the log file
  // returns nothing
  void writeTimestamp();
};

} // namespace crawler
} // namespace avengine
