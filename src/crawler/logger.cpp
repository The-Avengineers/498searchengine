#include "../../include/avengine/crawler/logger.h"

namespace avengine
{
namespace crawler
{

CrawlLogger::CrawlLogger(const char* path) : logFile(path, std::ios::app) {}

CrawlLogger::~CrawlLogger()
{
  lock_guard<mutex> guard(logMutex);
  logFile.close();
}

void CrawlLogger::writeTimestamp()
{
  time_t now = time(nullptr);
  struct tm* t = gmtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", t);
  logFile << buf;
}

} // namespace crawler
} // namespace avengine
