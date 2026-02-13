#include "index.h"
#include <cassert>
#include <cctype>
#include <cmath>
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

static void append_var_uint32(avengine::vector<uint8_t>& out, uint32_t value)
{
  while (value >= 0x80u)
  {
    out.pushBack(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  out.pushBack(static_cast<uint8_t>(value));
}

struct SkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

PostingList* Index::getBucket(char const* word)
{
  PostingList** bucket = &buckets[djb2Hash(word) % HASHMAP_SIZE];

  while (*bucket && strcmp((*bucket)->word, word))
  {
    bucket = &(*bucket)->nextBucket;
  }
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

  // Now we write out the posting list
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = bucket->hash;

      dictionaryBuffer[2 + hash * 2 + 1] = postingsOffset;

      const uint64_t documentFrequency = bucket->postings.size();
      const uint64_t collectionFrequency = bucket->positions.size();

      // Encode position list as varint deltas.
      avengine::vector<uint8_t> deltaBuffer;
      uint32_t lastPosition = 0;
      const uint32_t skipStride =
          collectionFrequency > 0 ? static_cast<uint32_t>(std::sqrt(collectionFrequency)) : 0;

      // Skip pointers are sampled every sqrt(N) positions. Each skip entry stores:
      // [position_index varint][absolute_position varint][delta_stream_byte_offset varint].
      avengine::vector<SkipPointer> skipPointers;
      for (uint32_t i = 0; i < collectionFrequency; i++)
      {
        uint32_t position = bucket->positions[i];
        if (skipStride > 1 && i > 0 && (i % skipStride) == 0)
        {
          skipPointers.pushBack({.positionIndex = i,
                                 .positionValue = position,
                                 .byteOffset = static_cast<uint32_t>(deltaBuffer.size())});
        }

        append_var_uint32(deltaBuffer, position - lastPosition);
        lastPosition = position;
      }

      avengine::vector<uint8_t> skipBuffer;
      for (const SkipPointer& skip : skipPointers)
      {
        append_var_uint32(skipBuffer, skip.positionIndex);
        append_var_uint32(skipBuffer, skip.positionValue);
        append_var_uint32(skipBuffer, skip.byteOffset);
      }

      // Per-term posting-list header:
      // [doc_frequency u64][collection_frequency u64][skip_count u64].
      uint8_t postingHeader[24];
      uint8_t* headerCursor = postingHeader;
      headerCursor = write_uint64(headerCursor, documentFrequency);
      headerCursor = write_uint64(headerCursor, collectionFrequency);
      write_uint64(headerCursor, skipPointers.size());
      write(fd, postingHeader, sizeof(postingHeader));

      if (!skipBuffer.empty())
        write(fd, skipBuffer.begin(), skipBuffer.size());
      if (!deltaBuffer.empty())
        write(fd, deltaBuffer.begin(), deltaBuffer.size());

      postingsOffset += sizeof(postingHeader);
      postingsOffset += skipBuffer.size();
      postingsOffset += deltaBuffer.size();

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
