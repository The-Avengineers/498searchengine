#pragma once
#include "../avengine_lib/deque.h"
#include "../avengine_lib/string.h"

namespace avengine
{

struct urlEntry
{
  string url;
  string domain;
  float priority;
  int depth; // from seed
  bool operator<(const urlEntry& other) const
  {
    return priority < other.priority;
  }
};

struct hostQueue
{
  string domain;
  priorityQueue<urlEntry, less<urlEntry>> pending;
  // URLs waiting to be fetched from this domain
};

struct heapEntry
{
  string domain;
  float priority;

  bool operator<(const heapEntry& other) const
  {
    return priority < other.priority;
  }
};

} // namespace avengine