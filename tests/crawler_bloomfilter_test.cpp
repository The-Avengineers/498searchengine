#include <avengine/avengine_lib/BloomFilter.h>

#include <stdio.h>
#include <stdlib.h>

namespace
{
void fail(const char* expr, int line)
{
  fprintf(stderr, "crawler_bloomfilter_test failed at line %d: %s\n", line, expr);
  abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)
}

int main()
{
  // Basic tests
  avengine::Bloomfilter filter(128, 0.01);

  // Check size
  EXPECT(filter.getSize() > 0);
  // Check contains
  EXPECT(!filter.contains("alpha"));

  // Check contains again after inserting it
  filter.insert("alpha");
  EXPECT(filter.contains("alpha"));
  // Check count and data pointer
  EXPECT(filter.getCount() == 1);
  EXPECT(filter.data() != nullptr);

  // Duplicate insert increments count
  filter.insert("alpha");
  EXPECT(filter.contains("alpha"));
  EXPECT(filter.getCount() == 2);

  // check const
  const avengine::Bloomfilter& constFilter = filter;
  EXPECT(constFilter.contains("alpha"));

  // Growth resize keeps buffer valid.
  const uint64_t oldSize = filter.getSize();
  filter.resize(oldSize + 4);
  EXPECT(filter.getSize() == oldSize + 4);
  EXPECT(filter.data() != nullptr);
  filter.insert("alpha");
  EXPECT(filter.contains("alpha"));

  // Shrink to zero updates exported size/pointer state.
  filter.resize(0);
  EXPECT(filter.getSize() == 0);
  EXPECT(filter.data() == nullptr);

  return 0;
}
