#include <avengine/avengine_lib/expected.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
void fail(const char* expr, int line)
{
  fprintf(stderr, "crawler_expected_test failed at line %d: %s\n", line, expr);
  abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

bool strEq(const avengine::string& lhs, const char* rhs)
{
  return strcmp(lhs.cstr(), rhs) == 0;
}
}

int main()
{
  // check value
  avengine::expected<int, avengine::error> ok = avengine::expected<int, avengine::error>::ok(42);
  EXPECT(ok.hasValue());
  EXPECT(ok.value() == 42);

  // check error
  avengine::expected<int, avengine::error> bad =
      avengine::expected<int, avengine::error>::error(avengine::error("boom"));
  EXPECT(!bad.hasValue());
  EXPECT(strEq(bad.error().msg, "boom"));

  // void return value
  avengine::expected<void, avengine::error> okVoid =
      avengine::expected<void, avengine::error>::ok();
  EXPECT(okVoid.hasValue());

  // void return error
  avengine::expected<void, avengine::error> badVoid =
      avengine::expected<void, avengine::error>::error(avengine::error("bruh"));
  EXPECT(!badVoid.hasValue());
  EXPECT(strEq(badVoid.error().msg, "bruh"));

  // copy constructor
  avengine::expected<int, avengine::error> copied(ok);
  EXPECT(copied.hasValue());
  EXPECT(copied.value() == 42);

  // assignment operator
  avengine::expected<int, avengine::error> assigned =
      avengine::expected<int, avengine::error>::error(avengine::error("tmp"));
  assigned = ok;
  EXPECT(assigned.hasValue());
  EXPECT(assigned.value() == 42);

  //copy void 
  avengine::expected<void, avengine::error> copiedVoid(badVoid);
  EXPECT(!copiedVoid.hasValue());
  EXPECT(strEq(copiedVoid.error().msg, "bruh"));

  // ensure value multable
  ok.value() = 99;
  EXPECT(ok.value() == 99);

  // ensure error mutable
  bad.error().msg = "changed";
  EXPECT(strEq(bad.error().msg, "changed"));

  return 0;
}
