#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include "string.h"
#include "vector.h"
#include <cstring>
#include <math.h>
#include <openssl/md5.h>
#include <stddef.h>
#include <stdint.h>

namespace avengine
{

class Bloomfilter
{
public:
  Bloomfilter(int num_objects, double false_positive_rate)
  {
    // Determine the size of bits of our data vector, and resize.
    count = 0;
    const double ln_2 = log(2);
    const double num_objects_double = static_cast<double>(num_objects);
    const double ln_fpr = log(false_positive_rate);
    size = static_cast<int64_t>(-(num_objects_double * ln_fpr / (ln_2 * ln_2)));
    uint64_t bytes = (size + 7) / 8;
    bits.reserve(bytes);
    for (uint64_t i = 0; i < bytes; ++i)
    {
      bits.pushBack(0);
    }

    // Determine number of hash functions to use.

    num_hash_funcs = static_cast<int64_t>((static_cast<double>(size) / num_objects_double) * ln_2);

    if (num_hash_funcs < 1)
    {
      num_hash_funcs = 1;
    }
  }

  void insert(const char* s)
  {
    // Hash the string into two unique hashes.

    hashPair h = hash(s);
    uint64_t hash1 = h.first;
    uint64_t hash2 = h.second;
    count++;
    // Use double hashing to get unique bit, and repeat for each hash function.

    for (uint64_t i{}; i < num_hash_funcs; ++i)
    {
      size_t idx = (hash1 + (i * hash2)) % size;
      bits[idx / 8] |= (1 << (idx % 8));
    }
  }

  void insert(const string& s)
  {
    insert(s.cstr());
  }

  bool contains(const char* s) const
  {
    // Hash the string into two unqiue hashes.

    hashPair h = hash(s);
    uint64_t hash1 = h.first;
    uint64_t hash2 = h.second;

    // Use double hashing to get unique bit, and repeat for each hash function.
    // If bit is false, we know for certain this unique string has not been
    // inserted.

    for (uint64_t i{}; i < num_hash_funcs; ++i)
    {
      size_t idx = (hash1 + (i * hash2)) % size;
      if (!(bits[idx / 8] & (1 << (idx % 8))))
      {
        return false;
      }
    }

    // If all bits were true, the string is likely inserted, but false positive
    // is possible.

    return true;
  }

  bool contains(const string& s) const
  {
    return contains(s.cstr());
  }

  uint64_t getSize() const
  {
    return bits.size();
  }

  uint64_t getCount() const
  {
    return count;
  }

  uint8_t* data()
  {
    if (bits.size() == 0)
    {
      return nullptr;
    }
    return bits.begin();
  }

  const uint8_t* data() const
  {
    if (bits.size() == 0)
    {
      return nullptr;
    }
    return bits.begin();
  }

  void resize(uint64_t bytes)
  {
    if (bytes > bits.size())
    {
      bits.reserve(bytes);
      while (bits.size() < bytes)
      {
        bits.pushBack(0);
      }
    }
    else
    {
      while (bits.size() > bytes)
      {
        bits.popBack();
      }
    }
    size = bytes * 8;
  }

private:
  struct hashPair
  {
    uint64_t first;
    uint64_t second;
  };

  // Add any private member variables that may be neccessary.
  uint64_t count;
  vector<uint8_t> bits;
  uint64_t size;
  uint64_t num_hash_funcs;

  hashPair hash(const char* datum) const
  {
    // Use MD5 to hash the string into one 128 bit hash, and split into 2
    // hashes.

    if (datum == nullptr)
    {
      return {0, 0};
    }

    size_t len = 0;
    while (datum[len] != '\0')
    {
      ++len;
    }

    unsigned char md_hash[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(datum), len, md_hash);

    uint64_t hash1;
    uint64_t hash2;
    memcpy(&hash1, md_hash, 8);
    memcpy(&hash2, md_hash + 8, 8);

    return {hash1, hash2};
  }
};
} // namespace avengine
#endif
