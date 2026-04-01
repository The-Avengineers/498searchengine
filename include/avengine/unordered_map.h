#pragma once

#include "avengine/avengine_lib/HashTable.h"
#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/vector.h"
#include <algorithm>
#include <avengine/avengine_lib/string_slice.h>

namespace avengine
{
inline size_t hashStringSlice(avengine::string_slice str)
{
  size_t hash = 5381;

  for (size_t i = 0; i < str.size(); i++)
    hash = ((hash << 5) + hash) + static_cast<unsigned char>(str[i]); /* hash * 33 + c */

  return hash;
}

template <class T> class unordered_map
{
  static constexpr float MAX_LOAD_FACTOR = 0.75f;

  struct Bucket
  {
    avengine::string key;
    T value;
    Bucket* nextBucket;

    Bucket(avengine::string key, T value, Bucket* next)
        : key(std::move(key)), value(std::move(value)), nextBucket(next)
    {
    }
  };

  avengine::vector<Bucket*> buckets;
  size_t elementCount = 0;

  // Delete all bucket chains and reset the table to a clean state.
  void destroyBuckets()
  {
    for (size_t i = 0; i < buckets.size(); i++)
    {
      Bucket* bucket = buckets[i];
      while (bucket)
      {
        Bucket* next = bucket->nextBucket;
        delete bucket;
        bucket = next;
      }
      buckets[i] = nullptr;
    }
  }

  void rehash(size_t newSize)
  {
    avengine::vector<Bucket*> newBuckets(newSize, nullptr);

    // Re-insert every existing bucket chain into the new table
    for (size_t i = 0; i < buckets.size(); i++)
    {
      Bucket* bucket = buckets[i];
      while (bucket)
      {
        Bucket* next = bucket->nextBucket;

        // Place bucket into the new table
        size_t newIndex = hashStringSlice(avengine::string_slice(bucket->key)) % newSize;
        bucket->nextBucket = newBuckets[newIndex];
        newBuckets[newIndex] = bucket;

        bucket = next;
      }
    }

    buckets = std::move(newBuckets);
  }

public:
  unordered_map() : buckets(256, nullptr) {}

  explicit unordered_map(size_t initialCapacity) : buckets(initialCapacity, nullptr) {}

  // Destructor: free every heap-allocated Bucket in every chain.
  ~unordered_map()
  {
    destroyBuckets();
  }

  // Deep-copy constructor: duplicate every bucket chain from other.
  unordered_map(const unordered_map& other)
      : buckets(other.buckets.size(), nullptr), elementCount(other.elementCount)
  {
    for (size_t i = 0; i < other.buckets.size(); i++)
    {
      Bucket** dest = &buckets[i];
      Bucket* src = other.buckets[i];
      while (src)
      {
        *dest = new Bucket(src->key, src->value, nullptr);
        dest = &(*dest)->nextBucket;
        src = src->nextBucket;
      }
    }
  }

  // Copy-assignment via copy-and-swap (provides strong exception safety).
  unordered_map& operator=(unordered_map other)
  {
    swap(*this, other);
    return *this;
  }

  // Move constructor: steal the bucket array; leave source in a valid empty state.
  unordered_map(unordered_map&& other) noexcept
      : buckets(std::move(other.buckets)), elementCount(other.elementCount)
  {
    other.elementCount = 0;
  }

  // Move-assignment: destroy our buckets, then steal from other.
  unordered_map& operator=(unordered_map&& other) noexcept
  {
    if (this != &other)
    {
      destroyBuckets();
      buckets = std::move(other.buckets);
      elementCount = other.elementCount;
      other.elementCount = 0;
    }
    return *this;
  }

  friend void swap(unordered_map& a, unordered_map& b) noexcept
  {
    using std::swap;
    swap(a.buckets, b.buckets);
    swap(a.elementCount, b.elementCount);
  }

  Bucket** getBucket(avengine::string_slice key)
  {
    Bucket** bucket = &buckets[hashStringSlice(key) % buckets.size()];

    while (*bucket && avengine::string_slice((*bucket)->key) != key)
    {
      bucket = &(*bucket)->nextBucket;
    }

    return bucket;
  }

  const Bucket* const* getBucket(avengine::string_slice key) const
  {
    const Bucket* const bucket = &buckets[hashStringSlice(key) % buckets.size()];

    while (*bucket && avengine::string_slice((*bucket)->key) != key)
    {
      bucket = &(*bucket)->nextBucket;
    }
  }

  size_t size() const
  {
    return elementCount;
  }

  // Calls func(key, value) for every entry in the map. The key and value
  // references are stable for the duration of the call (rehash never occurs
  // inside forEach).
  template <typename Func> void forEach(Func&& func)
  {
    for (size_t i = 0; i < buckets.size(); i++)
    {
      Bucket* bucket = buckets[i];
      while (bucket)
      {
        func(bucket->key, bucket->value);
        bucket = bucket->nextBucket;
      }
    }
  }

  bool insert(avengine::string key, T value)
  {
    // Look up the slot before moving from key/value.
    Bucket** bucket = getBucket(avengine::string_slice(key));

    if (!*bucket)
    {
      *bucket = new Bucket(std::move(key), std::move(value), nullptr);
      elementCount++;

      // Resize when load factor exceeds the threshold
      if (static_cast<float>(elementCount) / static_cast<float>(buckets.size()) > MAX_LOAD_FACTOR)
        rehash(buckets.size() * 2);

      return false;
    }

    (*bucket)->value = std::move(value);

    return true;
  }

  T* get(avengine::string_slice key)
  {
    Bucket** bucket = getBucket(key);
    return *bucket ? &(*bucket)->value : nullptr;
  }

  const T* get(avengine::string_slice key) const
  {
    const Bucket* const* bucket = getBucket(key);
    return *bucket ? &(*bucket)->value : nullptr;
  }

  T& operator[](avengine::string_slice key)
  {
    if (static_cast<float>(elementCount) / static_cast<float>(buckets.size()) > MAX_LOAD_FACTOR)
    {
      rehash(buckets.size() * 2);
    }

    Bucket** bucket = getBucket(avengine::string_slice(key));

    if (!*bucket)
    {
      *bucket = new Bucket(std::move(key), T{}, nullptr);
      elementCount++;
    }

    return (*bucket)->value;
  }
};
} // namespace avengine
