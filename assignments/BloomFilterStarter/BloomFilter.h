#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <vector>
#include <cmath>
#include <string>
#include <string.h>
#include <openssl/md5.h>
#include <vector>

class Bloomfilter
   {
   public:
      Bloomfilter( int num_objects, double false_positive_rate )
         {
         // Determine the size of bits of our data vector, and resize.

         const double ln_2 = std::log(2);
         const double num_objects_double = static_cast<double>(num_objects);
         const double ln_fpr = std::log(false_positive_rate);
         size = static_cast<int>(-(num_objects_double * ln_fpr / (ln_2 * ln_2)));
         bits.resize(size);
         // Determine number of hash functions to use.

         num_hash_funcs = static_cast<int>((static_cast<double>(size) / 
                              num_objects_double) * ln_2);
         
         if (num_hash_funcs < 1) { // just in case
            num_hash_funcs = 1;
         }
         
         }

      void insert( const std::string &s)
         {
         // Hash the string into two unique hashes.

         // Use double hashing to get unique bit, and repeat for each hash function.

         }

      bool contains( const std::string &s )
         {
         // Hash the string into two unqiue hashes.

         // Use double hashing to get unique bit, and repeat for each hash function.
         // If bit is false, we know for certain this unique string has not been inserted.

         // If all bits were true, the string is likely inserted, but false positive is possible.

         // This line is for compiling, replace this with own code.
         return false;
         }

   private:
      // Add any private member variables that may be neccessary.
      std::vector<bool> bits;
      int size;
      int num_hash_funcs;
      std::pair<uint64_t, uint64_t> hash( const std::string &datum )
         {
         //Use MD5 to hash the string into one 128 bit hash, and split into 2 hashes.
        
         //This line is for compiling, replace this with own code.
         return {0,0};
         }
   };

#endif