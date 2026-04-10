#pragma once

#include <avengine/avengine_lib/string.h>

namespace avengine
{
namespace search
{

static const size_t kDefaultSearchLimit = 10u;
static const size_t kMaxSearchPageSize = 10u;
static const size_t kMaxVisibleSearchResults = 100u;

inline bool IsBlankText(const string& value)
{
  for (size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] != ' ' && value[i] != '\t' && value[i] != '\r' && value[i] != '\n')
      return false;
  }

  return true;
}

inline size_t ClampSearchLimit(size_t limit)
{
  if (limit == 0 || limit > kMaxSearchPageSize)
    return kDefaultSearchLimit;
  return limit;
}

inline void AppendUnsigned(string& out, size_t value)
{
  char digits[32];
  size_t count = 0;
  do
  {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value > 0);

  while (count > 0)
    out.pushBack(digits[--count]);
}

} // namespace search
} // namespace avengine
