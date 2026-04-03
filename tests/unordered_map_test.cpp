#include "avengine/unordered_map.h"

#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/string_slice.h"

#include <cassert>

namespace
{

// Build a key like "key_0", "key_1", ..., "key_299"
avengine::string makeKey(int n)
{
  avengine::string key("key_");
  if (n == 0)
  {
    key.pushBack('0');
    return key;
  }
  char digits[12];
  int len = 0;
  while (n > 0)
  {
    digits[len++] = static_cast<char>('0' + (n % 10));
    n /= 10;
  }
  for (int i = len - 1; i >= 0; --i)
  {
    key.pushBack(digits[i]);
  }
  return key;
}

} // namespace

int main()
{
  // get on an empty map returns nullptr
  {
    avengine::unordered_map<int> map;
    assert(map.get(avengine::string_slice("missing")) == nullptr);
  }

  // insert a new key returns false (key did not previously exist)
  {
    avengine::unordered_map<int> map;
    bool existed = map.insert(avengine::string("alpha"), 1);
    assert(existed == false);
  }

  // get retrieves the value that was inserted
  {
    avengine::unordered_map<int> map;
    map.insert(avengine::string("alpha"), 42);
    int* val = map.get(avengine::string_slice("alpha"));
    assert(val != nullptr);
    assert(*val == 42);
  }

  // inserting the same key again returns true and updates the stored value
  {
    avengine::unordered_map<int> map;
    map.insert(avengine::string("alpha"), 1);
    bool existed = map.insert(avengine::string("alpha"), 99);
    assert(existed == true);
    int* val = map.get(avengine::string_slice("alpha"));
    assert(val != nullptr);
    assert(*val == 99);
  }

  // get returns nullptr for a key that was never inserted
  {
    avengine::unordered_map<int> map;
    map.insert(avengine::string("present"), 7);
    assert(map.get(avengine::string_slice("absent")) == nullptr);
  }

  // multiple distinct keys can all be retrieved independently
  {
    avengine::unordered_map<int> map;
    map.insert(avengine::string("a"), 1);
    map.insert(avengine::string("b"), 2);
    map.insert(avengine::string("c"), 3);

    int* a = map.get(avengine::string_slice("a"));
    int* b = map.get(avengine::string_slice("b"));
    int* c = map.get(avengine::string_slice("c"));

    assert(a != nullptr && *a == 1);
    assert(b != nullptr && *b == 2);
    assert(c != nullptr && *c == 3);
  }

  // can insert and retrieve with the [] operator
  {
    avengine::unordered_map<int> map;
    map[avengine::string_slice("a")] = 1;
    map[avengine::string_slice("b")] = 2;
    map[avengine::string_slice("c")] = 3;

    assert(map[avengine::string_slice("a")] == 1);
    assert(map[avengine::string_slice("b")] == 2);
    assert(map[avengine::string_slice("c")] == 3);
  }

  // [] copies the exact slice contents even when the source is not null-terminated
  {
    avengine::unordered_map<int> map;
    avengine::string keyBuffer("prefix-alpha-suffix");
    avengine::string_slice key(keyBuffer, 7, 5);

    map[key] = 17;

    int* val = map.get(avengine::string_slice("alpha"));
    assert(val != nullptr);
    assert(*val == 17);
    assert(map.get(avengine::string_slice("alpha-suffix")) == nullptr);
  }

  // inserting > 192 entries (0.75 * 256) triggers a rehash; all values
  // remain accessible with the correct values afterward
  {
    avengine::unordered_map<int> map;
    const int N = 300;

    for (int i = 0; i < N; ++i)
    {
      bool existed = map.insert(makeKey(i), i * 10);
      assert(existed == false);
    }

    for (int i = 0; i < N; ++i)
    {
      avengine::string key = makeKey(i);
      int* val = map.get(avengine::string_slice(key));
      assert(val != nullptr);
      assert(*val == i * 10);
    }
  }

  // a key that was never inserted still returns nullptr after a rehash
  {
    avengine::unordered_map<int> map;
    const int N = 300;

    for (int i = 0; i < N; ++i)
    {
      map.insert(makeKey(i), i);
    }

    assert(map.get(avengine::string_slice("never_inserted")) == nullptr);
  }

  // values updated before a rehash are still correct after
  {
    avengine::unordered_map<int> map;

    // Insert initial values
    const int EARLY = 10;
    for (int i = 0; i < EARLY; ++i)
    {
      map.insert(makeKey(i), i);
    }

    // Overwrite a subset of them
    for (int i = 0; i < EARLY; i += 2)
    {
      bool existed = map.insert(makeKey(i), i + 1000);
      assert(existed == true);
    }

    // Insert enough additional keys to force a rehash
    for (int i = EARLY; i < 300; ++i)
    {
      map.insert(makeKey(i), i);
    }

    // Updated values should still reflect the overwrite
    for (int i = 0; i < EARLY; ++i)
    {
      avengine::string key = makeKey(i);
      int* val = map.get(avengine::string_slice(key));
      assert(val != nullptr);
      if (i % 2 == 0)
      {
        assert(*val == i + 1000);
      }
      else
      {
        assert(*val == i);
      }
    }
  }

  // map works with a non-int value type (avengine::string as value)
  {
    avengine::unordered_map<avengine::string> map;
    map.insert(avengine::string("greeting"), avengine::string("hello"));
    map.insert(avengine::string("farewell"), avengine::string("goodbye"));

    avengine::string* g = map.get(avengine::string_slice("greeting"));
    avengine::string* f = map.get(avengine::string_slice("farewell"));

    assert(g != nullptr && *g == avengine::string("hello"));
    assert(f != nullptr && *f == avengine::string("goodbye"));
    assert(map.get(avengine::string_slice("missing")) == nullptr);
  }

  return 0;
}
