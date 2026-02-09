#include "index.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

size_t djb2Hash(char const* str)
{
  size_t hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

PostingList::PostingList(char const* word, PostingList* nextBucket) : nextBucket(nextBucket)

{
  this->word = new char[strlen(word) + 1]();
  strcpy(this->word, word);
}

void PostingList::ingest(uint32_t docId, size_t offset)
{
  if (postings.empty() || postings.back().docId != docId)
  {
    postings.pushBack({.docId = docId, .termFreq = 0, .positions = positions.size() - 1});
  }

  Posting& posting = postings.back();

  posting.termFreq++;
  positions.pushBack(offset);
}

uint8_t* write_var_uint32(uint8_t* buffer, uint32_t value)
{
  while (value >= 0x80u)
  {
    *buffer++ = static_cast<uint8_t>((value & 0x7Fu) | 0x80u);
    value >>= 7;
  }
  *buffer++ = static_cast<uint8_t>(value);
  return buffer;
}

uint8_t* write_uint64(uint8_t* buffer, uint64_t value)
{
  memcpy(buffer, &value, sizeof(uint64_t));
  return buffer + sizeof(uint64_t);
}

PostingList* Index::getBucket(char const* word)
{
  PostingList** bucket = &buckets[djb2Hash(word) % HASHMAP_SIZE];

  while (*bucket && strcmp((*bucket)->word, word))
  {
    bucket = &(*bucket)->nextBucket;
  }

  if (!*bucket)
  {
    totalUniqWords++;
    *bucket = new PostingList(word, nullptr);
  }

  return *bucket;
}

Index::Index() : buckets(HASHMAP_SIZE)
{
  for (PostingList*& bucket : buckets)
  {
    bucket = nullptr;
  }
}

void Index::serialize(int fd)
{
  size_t needed = 2;

  needed += (totalUniqWords + 1) * 2 * 2;

  uint64_t* dictionaryBuffer = new uint64_t[needed]();
  // TODO: We currently use an offset of zero as a sentinel is indicate an unused offset, however
  // this perfecty valid. We either need to use another sentinel value or make an offset of zero
  // impossible.

  // std::fill(dictionary_buffer, dictionary_buffer + needed, UINT64_MAX);

  // TODO: Write magic bytes to identity file format and version. Maybe an emoji?
  dictionaryBuffer[0] = documentNames.size();
  dictionaryBuffer[1] = totalUniqWords + 1;

  avengine::vector<char> stringBuffer;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = djb2Hash(bucket->word) % (totalUniqWords * 2);

      // Find an empty bucket
      while (dictionaryBuffer[2 + hash * 2] > 0)
      {
        // TODO: We should benchmark (when quering the index) on what kind of probing method
        // performs the best.
        hash += 1;
        hash %= (totalUniqWords * 2);
      }

      dictionaryBuffer[2 + hash * 2] = stringBuffer.size();

      // Add the term to the string buffer
      for (char const* str = bucket->word; *str; str++)
        stringBuffer.pushBack(*str);

      stringBuffer.pushBack('\0');

      bucket->hash = hash;
      bucket = bucket->nextBucket;
    }
  }

  // TODO: maybe fallocate?
  lseek(fd, needed * sizeof(uint64_t), SEEK_SET);

  write(fd, stringBuffer.begin(), stringBuffer.size());

  size_t postingsOffset = 0;

  // Allocate a buffer big buffer for writing, this should be changed to just what we need.
  uint8_t* postingBuffer = new uint8_t[16777216]();
  uint8_t* postingBufferHead = postingBuffer;

  // Now we write out the posting list
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = bucket->hash;

      dictionaryBuffer[2 + hash * 2 + 1] = postingsOffset;

      postingBufferHead = write_uint64(postingBufferHead, bucket->postings.size());
      // TODO: Write total collection frequency
      postingBufferHead = write_uint64(postingBufferHead, 0xdeadbeef);

      size_t lastPosition = 0;

      // TODO: Write index synchronization points and skip pointers.

      for (uint32_t position : bucket->positions)
      {
        postingBufferHead = write_var_uint32(postingBufferHead, position - lastPosition);
        lastPosition = position;
      }

      postingsOffset += postingBufferHead - postingBuffer;

      write(fd, postingBuffer, postingBufferHead - postingBuffer);

      postingBufferHead = postingBuffer;

      bucket = bucket->nextBucket;
    }
  }

  lseek(fd, 0, SEEK_SET);
  write(fd, dictionaryBuffer, needed * sizeof(uint64_t));
}

uint32_t Index::beginDocument(const char* url)
{
  documentNames.pushBack(url);
  currentDocId = static_cast<uint32_t>(documentNames.size() - 1);
  return currentDocId;
}

void Index::ingest(char const* word)
{
  assert(currentDocId != UINT32_MAX);
  PostingList* bucket = getBucket(word);

  bucket->ingest(currentDocId, offset++);
}

Index globalIndex;

// TODO: When string slice is merged we should use that everywhere instead of cstrings.
size_t indexDocument(char const* url, char* pharses)
{
  globalIndex.beginDocument(url);
  size_t words = 0;

  while (*pharses && !isalpha(*pharses))
    pharses++;

  while (*pharses)
  {
    char const* word = pharses;

    while (*pharses && isalpha(*pharses))
    {
      *pharses = *pharses | 32;
      pharses++;
    }

    *(pharses++) = '\0';

    globalIndex.ingest(word);
    words++;

    while (*pharses && !isalpha(*pharses))
      pharses++;
  }

  return words;
}
