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
};

struct hostQueue
{
  string domain;
  deque<urlEntry> pending;
  // URLs waiting to be fetched from this domain
};

}
