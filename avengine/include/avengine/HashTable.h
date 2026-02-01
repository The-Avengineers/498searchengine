// Simple hash table template.

// Nicole Hamilton  nham@umich.edu

#pragma once

#include <cassert>
#include <iostream>
#include <iomanip>
#include <cstdint>

namespace avengine {

using namespace std;


// You may add additional members or helper functions.


template< typename Key, typename Value > class Tuple
   {
   public:
      Key key;
      Value value;

      Tuple( const Key &k, const Value v ) : key( k ), value( v )
         {
         }
   };


template< typename Key, typename Value > class Bucket
   {
   public:
      Bucket *next;
      uint64_t hashValue;
      Tuple< Key, Value > tuple;

      Bucket( const Key &k, uint64_t h, const Value v ) :
            tuple( k, v ), next( nullptr ), hashValue( h )
         {
         }
   };


template< typename Key, typename Value > class HashTable
   {
   private:

      Bucket< Key, Value > **buckets;
      size_t numberOfBuckets;

      uint64_t ( *hash )( const Key );
      bool ( *compareEqual )( const Key, const Key );
      size_t uniqueKeys;

      friend class Iterator;
      friend class HashBlob;

      // YOUR CODE HERE


   public:

      Tuple< Key, Value > *Find( const Key k, const Value initialValue )
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) entry.  If the key is not already
         // in the hash, add it with the initial value.

         //hash the key, then use modular arithmetic to get the bucket index
         uint64_t hashValue = hash(k);
         uint64_t idx = hashValue % numberOfBuckets;
         Bucket<Key,Value> * pt = buckets[idx];


         //for while the bucket list exists at this index
         while(pt != nullptr) {
            //if the keys match, then return this tuple (key found!)
            if(compareEqual(pt->tuple.key, k)){
               return &pt->tuple;
            }
            pt = pt->next;
         }

         //if not found, then need to add the bucket
         Bucket<Key, Value> *newBucket = new Bucket<Key, Value>(k, hashValue, initialValue);
         newBucket->next = buckets[idx];
         buckets[idx] = newBucket;
         uniqueKeys++;
         return &newBucket->tuple;
         }

      Tuple< Key, Value > *Find( const Key k ) const
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) enty.  If the key is not already
         // in the hash, return nullptr.

         uint64_t hashValue = hash(k);
         uint64_t idx = hashValue % numberOfBuckets;
         Bucket<Key,Value> * pt = buckets[idx];

         while(pt != nullptr) {
            if(compareEqual(pt->tuple.key, k)){
               return &pt->tuple;
            }
            pt = pt->next;
         }

         //if the key isn't found, then return a nullptr
         return nullptr;
         }

      void Optimize( double loading = 1.5 )
         {
         // Grow or shrink the table as appropriate once we know the loading. A
         // goodrule of thumb is that the table size should be at least 1.5x the
         // number of unique keys.

         //set the newSize to be the loading * the number of unique keys (in this case 1.5)
         size_t newSize = (size_t)(uniqueKeys*loading);
         if(newSize <= 0){ newSize = 1; }
         
         //set all buckets of the new sized table to be null
         Bucket<Key, Value> ** newBuckets = new Bucket<Key, Value>*[newSize];
         for(size_t i = 0; i < newSize; ++i){
            newBuckets[i] = nullptr;
         }

         //go through the old hash table, re-compute the idx by modulus the hashed Value by the new size, and add each bucket to the corresponding index
         for(size_t i = 0; i < numberOfBuckets; ++i){
            Bucket<Key, Value> * curr = buckets[i];
            //if there's more than 1 bucket at this index (list) then add to the front of the list at this index (in the new hashtable)
            while(curr != nullptr){
               Bucket<Key, Value> * nxt = curr->next;
               uint64_t idx = curr->hashValue % newSize;
               curr->next = newBuckets[idx];
               newBuckets[idx] = curr;
               curr = nxt;
            }
         }

         //delete the old array (but keep the ptrs to buckets allocated)
         delete[] buckets;

         //set new mem vars
         this->buckets = newBuckets;
         this->numberOfBuckets = newSize;
         }


      // Your constructor may add defaults for arguments to the
      // constructor.  The compareEqual function should return
      // true if the keys are equal.  The hash function should
      // return a 64-bit value.

      HashTable( bool ( *compareEqual )( const Key, const Key ),
            uint64_t ( *hash )( const Key ),
            size_t numberOfBuckets )
         {
         //initialize a new empty hashtable
         this->hash = hash;
         this->compareEqual = compareEqual;
         this->numberOfBuckets = numberOfBuckets;
         this->buckets = new Bucket<Key, Value>*[numberOfBuckets];

         //start all buckets as null
         for(size_t i = 0; i < numberOfBuckets; ++i){
            this->buckets[i] = nullptr;
         }

         //empty so 0 uniquekeys
         this->uniqueKeys = 0;
         }


      ~HashTable( )
         {
            //have to go thru each bucket and delete individually
         for(size_t i = 0; i < numberOfBuckets; ++i){
            Bucket<Key,Value>* curr = buckets[i];
            while(curr != nullptr){
               Bucket<Key,Value> * nxt = curr->next;
               delete curr;
               curr = nxt;
            }
         }

         //then delete the buckets container
         delete[] buckets;
         }


      class Iterator
         {
         private:

            friend class HashTable;

            // YOUR CODE HERE
            HashTable *table;
            size_t bucketIdx;
            Bucket<Key,Value> *currentPtr;

            Iterator( HashTable *table, size_t bucket, Bucket<Key, Value> *b )
               {
               this->table = table;
               this->bucketIdx = bucket;
               this->currentPtr = b;
               }

         public:

            Iterator( ) : Iterator( nullptr, 0, nullptr )
               {
               }

            ~Iterator( )
               {
               }

            Tuple< Key, Value > &operator*( )
               {
                  return currentPtr->tuple;
               }

            Tuple< Key, Value > *operator->( ) const
               {
                  return &currentPtr->tuple;
               }

            // Prefix ++
            Iterator &operator++( )
               {
                  if(currentPtr->next != nullptr){
                     this->currentPtr = currentPtr->next;
                     return *this;
                  }

                  this->bucketIdx++;
                  while(bucketIdx < table->numberOfBuckets && table->buckets[bucketIdx] == nullptr){
                     this->bucketIdx++;
                  }

                  if(bucketIdx < table->numberOfBuckets){
                     this->currentPtr = table->buckets[bucketIdx];
                  }else{
                     this->currentPtr = nullptr;
                  }

                  return *this;
               }

            // Postfix ++
            Iterator operator++( int )
               {
               Iterator temp = *this;
               ++(*this);
               return temp;
               }

            bool operator==( const Iterator &rhs ) const
               {
                  return table == rhs.table && bucketIdx == rhs.bucketIdx && currentPtr == rhs.currentPtr;
               }

            bool operator!=( const Iterator &rhs ) const
               {
                  return !(*this == rhs);
               }
         };

      Iterator begin( )
         {
            for(size_t i = 0; i < this->numberOfBuckets; ++i){
               if(buckets[i] != nullptr){
                  return Iterator(this, i, buckets[i]);
               }
            }
            return Iterator(this, this->numberOfBuckets, nullptr);
         }

      Iterator end( )
         {
            return Iterator(this, this->numberOfBuckets, nullptr);
         }
   };
}
