// Simple hash table template.

// Nicole Hamilton  nham@umich.edu

#pragma once

#include <cassert>
#include <iostream>
#include <iomanip>
#include <cstdint>

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

         // YOUR CODE HERE

         return nullptr;
         }

      Tuple< Key, Value > *Find( const Key k ) const
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) enty.  If the key is not already
         // in the hash, return nullptr.

         // YOUR CODE HERE

         return nullptr;
         }

      void Optimize( double loading = 1.5 )
         {
         // Grow or shrink the table as appropriate once we know the loading. A
         // goodrule of thumb is that the table size should be at least 1.5x the
         // number of unique keys.

         // YOUR CODE HERE
         }


      // Your constructor may add defaults for arguments to the
      // constructor.  The compareEqual function should return
      // true if the keys are equal.  The hash function should
      // return a 64-bit value.

      HashTable( bool ( *compareEqual )( const Key, const Key ),
            uint64_t ( *hash )( const Key ),
            size_t numberOfBuckets )
         {
         // YOUR CODE HERE
         }


      ~HashTable( )
         {
         // YOUR CODE HERE
         }


      class Iterator
         {
         private:

            friend class HashTable;

            // YOUR CODE HERE

            Iterator( HashTable *table, size_t bucket, Bucket<Key, Value> *b )
               {
               // YOUR CODE HERE
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
               // YOUR CODE HERE
               }

            Tuple< Key, Value > *operator->( ) const
               {
               // YOUR CODE HERE
               }

            // Prefix ++
            Iterator &operator++( )
               {
               // YOUR CODE HERE
               }

            // Postfix ++
            Iterator operator++( int )
               {
               // YOUR CODE HERE
               }

            bool operator==( const Iterator &rhs ) const
               {
               // YOUR CODE HERE
               }

            bool operator!=( const Iterator &rhs ) const
               {
               // YOUR CODE HERE
               }
         };

      Iterator begin( )
         {
         // YOUR CODE HERE
         }

      Iterator end( )
         {
         // YOUR CODE HERE
         }
   };
