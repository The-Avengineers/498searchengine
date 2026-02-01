#pragma once

// HashBlob, a serialization of a HashTable into one contiguous
// block of memory, possibly memory-mapped to a HashFile.

// Nicole Hamilton  nham@umich.edu

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <sys/mman.h>

#include <avengine/HashTable.h>
#include <avengine/Common.h>

namespace avengine {


using Hash = HashTable< const char *, size_t >;
using Pair = Tuple< const char *, size_t >;
using HashBucket = Bucket< const char *, size_t >;

//magic number and version that we chose
size_t MAGIC_NUMBER = 6767;
size_t VERSION = 67;

static const size_t Unknown = 0;


size_t RoundUp( size_t length, size_t boundary )
   {
   // Round up to the next multiple of the boundary, which
   // must be a power of 2.

   static const size_t oneless = boundary - 1,
      mask = ~( oneless );
   return ( length + oneless ) & mask;
   }


struct SerialTuple
   {
   // This is a serialization of a HashTable< char *, size_t >::Bucket.
   // One is packed up against the next in a HashBlob.

   // Since this struct includes size_t and uint64_t members, we'll
   // require that it be sizeof( size_t ) aligned to avoid unaligned
   // accesses.

   // Alternatives might be to use #pragma pack() and pay the perf
   // penalty or use a Utf8-style variable-length encoding that
   // does not need to be aligned.

   public:

      // SerialTupleLength = 0 is a sentinel indicating
      // this is the last SerialTuple chained in this list.
      // (Actual length is not given but not needed.)

      size_t Length, Value;
      uint64_t HashValue;

      // The Key will be a C-string of whatever length.
      char Key[ Unknown ];

      // Calculate the bytes required to encode a HashBucket as a
      // SerialTuple.

      static size_t BytesRequired( const HashBucket *b )
         {
         
         //get the length of the key -> c string + the sentinel
         size_t keySize = strlen(b->tuple.key) + 1; 
         //add the length of the rest of the memebera vars
         size_t total = sizeof(Length) + sizeof(Value) + sizeof(HashValue) + keySize;
         return RoundUp(total, sizeof(size_t));
         }

      // Write the HashBucket out as a SerialTuple in the buffer,
      // returning a pointer to one past the last character written.

      static char *Write( char *buffer, char *bufferEnd,
            const HashBucket *b )
         {
         //get the required bytes for the bucket
         size_t requiredBytes = BytesRequired(b);
         //ensure that there's enough room in the buffer for the bucket
         assert(bufferEnd - buffer >= requiredBytes);
         
         //cast buffer to SerialTuple + fille in the fields
         SerialTuple * serialTup = (SerialTuple*)buffer;
         serialTup->Length = requiredBytes;
         serialTup->Value = b->tuple.value;
         serialTup->HashValue = b->hashValue;
         strcpy(serialTup->Key, b->tuple.key);
         
         //will point to 1 after the last char in key (requiredBytes includes sentinel)
         return buffer + requiredBytes;
         }
  };


class HashBlob
   {
   // This will be a hash specifically designed to hold an
   // entire hash table as a single contiguous blob of bytes.
   // Pointers are disallowed so that the blob can be
   // relocated to anywhere in memory

   // The basic structure should consist of some header
   // information including the number of buckets and other
   // details followed by a concatenated list of all the
   // individual lists of tuples within each bucket.

   public:

      // Define a MagicNumber and Version so you can validate
      // a HashBlob really is one of your HashBlobs.

      size_t MagicNumber,
         Version,
         BlobSize,
         NumberOfBuckets,
         Buckets[ Unknown ];

      // The SerialTuples will follow immediately after.

      const SerialTuple *Find( const char *key ) const
         {
         // Search for the key k and return a pointer to the
         // ( key, value ) entry.  If the key is not found,
         // return nullptr.

         //YOUR CODE HERE
         //use the hash function from common (the fnv-1a)
         uint64_t hashed = hashFunc(key);
         uint64_t idx = hashed % NumberOfBuckets;
         //buckets[idx] = 0 indicates that this idx isn't occupied
         if(Buckets[idx] == 0){
            return nullptr;
         }
         //point to this list of buckets (start of buffer + offset)
         char * pt = ((char *)this) + Buckets[idx];
         SerialTuple * thatIdx = (SerialTuple*)(pt);
         //checks if the list is still going (length == 0 is the sentinel)
         while(thatIdx->Length != 0){
            //if keys are equal return this serialtuple
            if(strcmp(thatIdx->Key, key) == 0){
               return thatIdx;
            }
            //go to the new serialtuple in this list
            pt += thatIdx->Length;
            thatIdx = (SerialTuple*)(pt);
         }
         return nullptr;
         }


      static size_t BytesRequired( const Hash *hashTable )
         {
         // Calculate how much space it will take to
         // represent a HashTable as a HashBlob.

         // Need space for the header + buckets +
         // all the serialized tuples.

         // YOUR CODE HERE
         //size of the header and buckets
         size_t sz = sizeof(size_t)*(4 + hashTable->numberOfBuckets);
         //getting size of each serialized tuple
         for(size_t i = 0; i < hashTable->numberOfBuckets; ++i){
            HashBucket * buck = hashTable->buckets[i];
            if(buck != nullptr){
               //add sentinels for non-empty lists
               sz += sizeof(size_t);
            }

            while (buck != nullptr){
               sz += SerialTuple::BytesRequired(buck);
               buck = buck->next;
            }

         }

         return sz;
         }

      // Write a HashBlob into a buffer, returning a
      // pointer to the blob.

      static HashBlob *Write( HashBlob *hb, size_t bytes,
            const Hash *hashTable )
         {
         // YOUR CODE HERE
         //fill in the header of the blob
         hb->MagicNumber = MAGIC_NUMBER;
         hb->Version = VERSION;
         hb->NumberOfBuckets = hashTable->numberOfBuckets;
         hb->BlobSize = BytesRequired(hashTable);
         //calcualte the overhead of the header to know where to start the serialtuples
         size_t offset = (sizeof(size_t)*4 + sizeof(size_t)*hashTable->numberOfBuckets);
         //calcualte the buffer start and end
         char* buffer = (char*)hb + offset;
         char* bufferEnd = (char*)hb + bytes;
         //go through the bcukets within the hashtable
         for(size_t i = 0; i < hashTable->numberOfBuckets; ++i){
            HashBucket * buck = hashTable->buckets[i];
            //if that bucket isn't occupied, have a 0 offset indicating it's empty
            if(buck == nullptr){
               hb->Buckets[i] = 0;
               continue;
            }
            
            //otherwise add the offset (the difference between the location of the st and the start of the hashblob)
            hb->Buckets[i] = buffer - (char*)hb;
            while (buck != nullptr){
               buffer = SerialTuple::Write(buffer, bufferEnd , buck);
               buck = buck->next;
            }
            
            //add sentinels
            *(size_t*)buffer = 0;
            buffer += sizeof(size_t);

         }
         
         return hb;
         }

      // Create allocates memory for a HashBlob of required size
      // and then converts the HashTable into a HashBlob.
      // Caller is responsible for discarding when done.

      // (No easy way to override the new operator to create a
      // variable sized object.)

      static HashBlob *Create( const Hash *hashTable )
         {
         // YOUR CODE HERE
         size_t bytes = BytesRequired(hashTable);
         HashBlob * hb = (HashBlob*) new char[bytes];
         hb = Write(hb,  bytes, hashTable);

         return hb;
         }

      // Discard

      static void Discard( HashBlob *blob )
         {
         char* hb = (char*)blob;
         delete[] hb;
         }
   };

class HashFile
   {
   private:

      HashBlob *blob;
      int fd; size_t fileSize;

      size_t FileSize( int f )
         {
         struct stat fileInfo;
         fstat( f, &fileInfo );
         return fileInfo.st_size;
         }

   public:

      const HashBlob *Blob( )
         {
         return blob;
         }

      HashFile( const char *filename )
         {
         // Open the file for reading, map it and check the header.
         // new a HashFile, fill in the filehandle and blob address.

         // YOUR CODE HERE
         fd = open(filename, O_RDONLY);
         assert(fd >= 0);
         
         fileSize = FileSize(fd);
         //mmap(void * addr, size_t length, int prot, int flags, int fd, off_t offset)
         blob = (HashBlob *) mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
         assert( blob != MAP_FAILED);
         assert(blob->MagicNumber == MAGIC_NUMBER);
         assert(blob->Version == VERSION);
         
         }

      HashFile( const char *filename, const Hash *hashtable )
         {
         // Open the file for write, map it, write
         // the hashtable out as a HashBlob, and note
         // the blob address.

         // YOUR CODE HERE
         fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
         assert(fd >= 0);
         fileSize = HashBlob::BytesRequired(hashtable);
         ftruncate(fd, fileSize);
         HashBlob * map = (HashBlob *) mmap(nullptr, fileSize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
         blob = HashBlob::Write(map, fileSize, hashtable);
         }

      ~HashFile( )
         {
         // YOUR CODE HERE
         munmap(blob, fileSize);
         close(fd);
         }
   };
}
