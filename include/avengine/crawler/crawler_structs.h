#pragma once
#include "../avengine_lib/string.h"

namespace avengine
{

struct urlEntry
{
  string url;
  string domain;
  int depth; // from seed
};

} // namespace avengine
