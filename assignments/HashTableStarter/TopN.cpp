// TopN.cpp
// Nicole Hamilton nham@umich.edu

// Given a hashtable, return a dynamically-allocated array
// of pointers to the top N pairs based on their values.
// Caller is responsible for deleting the array.

// Individual pairs are to be C-strings as keys and size_t's
// as values.

#include "HashTable.h"
#include "TopN.h"

using namespace std;


using Hash = HashTable< const char *, size_t >;
using Pair = Tuple< const char *, size_t >;


Pair **TopN( Hash *hashtable, int N )
   {
   // Find the top N pairs based on the values and return
   // as a dynamically-allocated array of pointers.  If there
   // are less than N pairs in the hash, remaining pointers
   // will be null.


   Pair ** top = new Pair*[N];
   for(size_t i = 0 ; i < N; ++i){
      top[i] = nullptr;
   }

   Hash::Iterator it = hashtable->begin();
   while(it != hashtable->end()){
      size_t val = it->value;
      for(size_t i = 0; i < N; ++i){
         if(top[i] == nullptr){
            top[i] = &(*it);
         }else if(val > top[i]->value){
            for(size_t j = N - 1; j > i; --j){
               top[j] = top[j - 1];
            }
            top[i] = &(*it);
            break;
         }
      }
      it++;
   }

   return top;
   }
