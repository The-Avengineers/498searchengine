#pragma once

#include "../avengine_lib/lock_guard.h"
#include "../avengine_lib/mutex.h"
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
  CrawlLogger(const char* path);
  ~CrawlLogger();

  // Writes a timestamp followed by each argument as a comma-separated field.
  // Arguments are streamed as-is — the caller is responsible for ensuring that
  // string values do not contain commas or newlines that would corrupt the CSV.
  template <typename... Args> void log(Args&&... args)
  {
    lock_guard<mutex> guard(logMutex);
    if (!logFile.is_open())
    {
      return;
    }
    writeTimestamp();
    ((logFile << ',' << std::forward<Args>(args)), ...);
    logFile << std::endl;
  }

private:
  std::ofstream logFile;
  mutex logMutex;

  void writeTimestamp();
};

} // namespace crawler
} // namespace avengine
