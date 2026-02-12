#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <cmath>
#include <openssl/md5.h>
#include <string.h>
#include <vector.h>

namespace avengine
{

class Bloomfilter {
public:
  Bloomfilter(int num_objects, double false_positive_rate) {
    // Determine the size of bits of our data vector, and resize.

    const double ln_2 = std::log(2);
    const double num_objects_double = static_cast<double>(num_objects);
    const double ln_fpr = std::log(false_positive_rate);
    size = static_cast<int64_t>(-(num_objects_double * ln_fpr / (ln_2 * ln_2)));
    bits.resize(size);

    // Determine number of hash functions to use.

    num_hash_funcs = static_cast<int64_t>(
        (static_cast<double>(size) / num_objects_double) * ln_2);

    if (num_hash_funcs < 1) {
      num_hash_funcs = 1;
    }
  }

  void insert(const std::string &s) {
    // Hash the string into two unique hashes.

    std::pair<uint64_t, uint64_t> h = hash(s);
    uint64_t hash1 = h.first;
    uint64_t hash2 = h.second;

    // Use double hashing to get unique bit, and repeat for each hash function.

    for (uint64_t i{}; i < num_hash_funcs; ++i) {
      size_t idx = (hash1 + (i * hash2)) % size;
      bits[idx] = true;
    }
  }

  bool contains(const std::string &s) {
    // Hash the string into two unqiue hashes.

    std::pair<uint64_t, uint64_t> h = hash(s);
    uint64_t hash1 = h.first;
    uint64_t hash2 = h.second;

    // Use double hashing to get unique bit, and repeat for each hash function.
    // If bit is false, we know for certain this unique string has not been
    // inserted.

    for (uint64_t i{}; i < num_hash_funcs; ++i) {
      size_t idx = (hash1 + (i * hash2)) % size;
      if (!bits[idx]) {
        return false;
      }
    }

    // If all bits were true, the string is likely inserted, but false positive
    // is possible.

    return true;
  }

  int get_size() {
    return size;
  }

private:
  // Add any private member variables that may be neccessary.

  std::vector<bool> bits;
  uint64_t size;
  uint64_t num_hash_funcs;

  std::pair<uint64_t, uint64_t> hash(const std::string &datum) {
    // Use MD5 to hash the string into one 128 bit hash, and split into 2
    // hashes.

    unsigned char md_hash[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char *>(datum.c_str()), datum.length(),
        md_hash);

    uint64_t hash1;
    uint64_t hash2;
    memcpy(&hash1, md_hash, 8);
    memcpy(&hash2, md_hash + 8, 8);

    return {hash1, hash2};
  }
};
}
#endif