#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/Utf8.h>

using namespace avengine;

TEST_CASE("UTF-8 ASCII round-trip")
{
  Utf8 buffer[8] = {0};
  Utf8* end = WriteUtf8(buffer, static_cast<Unicode>('A'));
  CHECK(end == buffer + 1);
  CHECK(buffer[0] == static_cast<Utf8>('A'));

  const Utf8* p = buffer;
  CHECK(ReadUtf8(&p, end) == static_cast<Unicode>('A'));
  CHECK(p == end);
}

TEST_CASE("UTF-8 malformed start byte returns replacement character")
{
  Utf8 bytes[] = {0xFF, 0x61};
  const Utf8* p = bytes;
  CHECK(ReadUtf8(&p, bytes + 2) == ReplacementCharacter);
  CHECK(p == bytes + 1);
  CHECK(ReadUtf8(&p, bytes + 2) == static_cast<Unicode>('a'));
}
