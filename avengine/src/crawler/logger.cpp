#include "../../include/avengine/crawler/logger.h"


// NOTE NEED TO ADD MUTEX FOR THIS 

namespace avengine
{
namespace crawler
{

CrawlLogger::CrawlLogger(const char* path)
    : logFile(path, std::ios::app)
{
}

CrawlLogger::~CrawlLogger()
{
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
  if (!logFile.is_open()) 
  {
    return;
  }
  writeTimestamp();
  logFile << "," << msg << "," << url 
        << std::endl;
  return;
}

// Print Url with a message and http status code
void CrawlLogger::log(const string& url, const char* msg, int statusCode)
{
  if (!logFile.is_open()) 
  {
    return;
  }
  writeTimestamp();
  logFile << "," << msg << "," << url << "," << statusCode 
        << std::endl;
  return;
}

// Print URL with a good message and the number of bytes after resp 200
void CrawlLogger::log(const string& url, const char* msg, size_t bytes)
{
  if (!logFile.is_open()) 
  {
    return;
  }
  writeTimestamp();
  logFile << "," << msg << "," << url << "," << bytes 
        << std::endl;
  return;
}

// print checkpoint with crawl count
void CrawlLogger::log(const char* event, size_t count, size_t seenCount, size_t listSize)
{
  if (!logFile.is_open()) 
  {
    return;
  }
  writeTimestamp();
  logFile << "," << event << "," << count << "," << seenCount << "," 
        << listSize << std::endl;
  return;
}

// Final checkpoint
void CrawlLogger::log(const char* event, size_t count, size_t seenCount)
{
  if (!logFile.is_open()) 
  {
    return;
  }
  writeTimestamp();
  logFile << "," << event << "," << count << "," << seenCount 
        << std::endl;
  return;
}

} 
}
