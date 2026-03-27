#include "../../include/avengine/crawler/logger.h"
#include "../../include/avengine/avengine_lib/lock_guard.h"
#include "../../include/avengine/avengine_lib/mutex.h"

// NOTE NEED TO ADD MUTEX FOR THIS

namespace
{
avengine::mutex gLoggerMutex;
}

namespace avengine
{
namespace crawler
{

CrawlLogger::CrawlLogger(const char* path) : logFile(path, std::ios::app) {}

CrawlLogger::~CrawlLogger()
{
  lock_guard<mutex> guard(gLoggerMutex);
  logFile.close();
}

// Write timestamp to logfile
void CrawlLogger::writeTimestamp()
{
  time_t now = time(nullptr);
  struct tm* t = gmtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", t);
  logFile << buf;
  return;
}

// Print URL with a message
void CrawlLogger::log(const string& url, const char* msg)
{
  lock_guard<mutex> guard(gLoggerMutex);
  if (!logFile.is_open())
  {
    return;
  }
  writeTimestamp();
  logFile << "," << msg << "," << url << std::endl;
  return;
}

// Print Url with a message and http status code
void CrawlLogger::log(const string& url, const char* msg, int statusCode)
{
  lock_guard<mutex> guard(gLoggerMutex);
  if (!logFile.is_open())
  {
    return;
  }
  writeTimestamp();
  logFile << "," << msg << "," << url << "," << statusCode << std::endl;
  return;
}

// Print URL with a good message and the number of bytes after resp 200
void CrawlLogger::log(const string& url, const char* msg, size_t bytes)
{
  lock_guard<mutex> guard(gLoggerMutex);
  if (!logFile.is_open())
  {
    return;
  }
  writeTimestamp();
  logFile << "," << msg << "," << url << "," << bytes << std::endl;
  return;
}

// print checkpoint with crawl count
void CrawlLogger::log(const char* event, size_t count, size_t seenCount, size_t listSize)
{
  lock_guard<mutex> guard(gLoggerMutex);
  if (!logFile.is_open())
  {
    return;
  }
  writeTimestamp();
  logFile << "," << event << "," << count << "," << seenCount << "," << listSize << std::endl;
  return;
}

// Final checkpoint
void CrawlLogger::log(const char* event, size_t count, size_t seenCount)
{
  lock_guard<mutex> guard(gLoggerMutex);
  if (!logFile.is_open())
  {
    return;
  }
  writeTimestamp();
  logFile << "," << event << "," << count << "," << seenCount << std::endl;
  return;
}

// void CrawlLogger::logIndexStats(size_t indexedCount, size_t indexBytes, size_t frontierSize)
// {
//   if (!logFile.is_open())
//   {
//     return;
//   }
//   writeTimestamp();
//   logFile << ",INDEX_STATS," << indexedCount << "," << indexBytes << "," << frontierSize
//           << std::endl;
// }

} // namespace crawler
} // namespace avengine
