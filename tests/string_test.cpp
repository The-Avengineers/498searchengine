#include "avengine/avengine_lib/string.h"

#include <cassert>
#include <utility>

namespace
{
using avengine::string;

string makeRepeat(char c, size_t count)
{
  string out;
  for (size_t i = 0; i < count; ++i)
  {
    out.pushBack(c);
  }
  return out;
}
} // namespace

int main()
{
  // Empty/default behavior
  string empty;
  assert(empty.size() == 0);
  assert(empty.cstr()[0] == '\0');

  // Basic literal and append behavior
  string hello("hello");
  string space(" ");
  string world("world");
  hello += space;
  hello += world;
  assert(hello == string("hello world"));

  // SSO boundary: should support 23 in small representation.
  string ssoBoundary = makeRepeat('a', 23);
  assert(ssoBoundary.size() == 23);
  const char* ssoPtr = ssoBoundary.cstr();
  for (size_t i = 0; i < 23; ++i)
  {
    assert(ssoPtr[i] == 'a');
  }
  assert(ssoPtr[23] == '\0');

  // Crossing the SSO boundary should still preserve contents.
  ssoBoundary.pushBack('b');
  assert(ssoBoundary.size() == 24);
  assert(ssoBoundary[0] == 'a');
  assert(ssoBoundary[22] == 'a');
  assert(ssoBoundary[23] == 'b');
  assert(ssoBoundary.cstr()[24] == '\0');

  // popBack on heap path
  ssoBoundary.popBack();
  assert(ssoBoundary.size() == 23);
  assert(ssoBoundary[22] == 'a');

  // Self-append should duplicate without corruption.
  string twice("abc");
  twice += twice;
  assert(twice == string("abcabc"));

  // Copy and move semantics
  string copied(twice);
  assert(copied == twice);
  assert(copied == "abcabc");
  assert(copied != "abc");

  string moved(std::move(copied));
  assert(moved == string("abcabc"));

  string assigned;
  assigned = moved;
  assert(assigned == string("abcabc"));

  string moveAssigned;
  moveAssigned = std::move(assigned);
  assert(moveAssigned == string("abcabc"));

  return 0;
}
