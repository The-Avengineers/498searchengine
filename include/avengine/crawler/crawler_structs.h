#pragma once
#include "../avengine_lib/string.h"

namespace avengine
{

struct urlEntry
{
  string url;
  string domain;
  int depth = 0;           // distance from original seed URL
  int seedDomainDepth = 0; // number of cross-domain transitions from the seed URL
  int seedSourceLine = 0;  // 1-indexed line number in `seed_list.txt`
};

} // namespace avengine
