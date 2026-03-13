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

void append_var_uint32(avengine::vector<uint8_t>& out, uint32_t value)
{
  while (value >= 0x80u)
  {
    out.pushBack(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  out.pushBack(static_cast<uint8_t>(value));
}

static void append_bytes(avengine::vector<uint8_t>& out, const void* data, size_t size)
{
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; i++)
  {
    out.pushBack(bytes[i]);
  }
}

void append_uint64(avengine::vector<uint8_t>& out, uint64_t value)
{
  append_bytes(out, &value, sizeof(uint64_t));
}

static void append_string(avengine::vector<uint8_t>& out, const char* str)
{
  if (!str)
  {
    append_var_uint32(out, 0);
    return;
  }

  const size_t len = strlen(str);
  append_var_uint32(out, static_cast<uint32_t>(len));
  append_bytes(out, str, len);
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

// Encodes a single posting list into `out` (cleared on entry):
//   [doc_freq u64][collection_freq u64][skip_count u64]
//   [skip stream varint*][position delta stream varint*]
static void encodePostingList(const PostingList& pl, avengine::vector<uint8_t>& out)
{
  avengine::vector<uint8_t> deltaBuffer;
  avengine::vector<SkipPointer> skipPointers;
  avengine::vector<uint8_t> skipBuffer;

  const uint64_t collectionFrequency = pl.positions.size();
  const uint32_t skipStride =
      collectionFrequency > 0 ? static_cast<uint32_t>(std::sqrt(collectionFrequency)) : 0;

  // Encode position list as varint deltas, sampling skip pointers every sqrt(N).
  uint32_t lastPosition = 0;
  for (uint32_t i = 0; i < collectionFrequency; i++)
  {
    uint32_t position = pl.positions[i];
    if (skipStride > 1 && i > 0 && (i % skipStride) == 0)
    {
      skipPointers.pushBack({.positionIndex = i,
                             .positionValue = position,
                             .byteOffset = static_cast<uint32_t>(deltaBuffer.size())});
    }
    append_var_uint32(deltaBuffer, position - lastPosition);
    lastPosition = position;
  }

  for (const SkipPointer& skip : skipPointers)
  {
    append_var_uint32(skipBuffer, skip.positionIndex);
    append_var_uint32(skipBuffer, skip.positionValue);
    append_var_uint32(skipBuffer, skip.byteOffset);
  }

  // Header: [doc_frequency u64][collection_frequency u64][skip_count u64].
  out.clear();
  append_uint64(out, pl.postings.size());
  append_uint64(out, collectionFrequency);
  append_uint64(out, skipPointers.size());
  append_bytes(out, skipBuffer.begin(), skipBuffer.size());
  append_bytes(out, deltaBuffer.begin(), deltaBuffer.size());
}

void Index::buildStringTable(uint64_t* dict, avengine::vector<char>& stringBuffer)
{
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      size_t hash = djb2Hash(bucket->word) % (totalUniqWords * 2);

      // Linear probe for an empty slot.
      // TODO: Benchmark alternative probing strategies when querying the index.
      while (dict[2 + hash * 2] > 0)
      {
        hash += 1;
        hash %= (totalUniqWords * 2);
      }

      dict[2 + hash * 2] = stringBuffer.size();

      for (char const* str = bucket->word; *str; str++)
        stringBuffer.pushBack(*str);
      stringBuffer.pushBack('\0');

      bucket->hash = hash;
      bucket = bucket->nextBucket;
    }
  }
}

bool Index::serialize(int fd)
{
  // TODO: We currently use an offset of zero as a sentinel to indicate an unused slot, however
  // zero is a perfectly valid offset. We either need another sentinel or make zero impossible.
  const size_t needed = 2 + (totalUniqWords + 1) * 2 * 2;
  uint64_t* dictionaryBuffer = new uint64_t[needed]();

  // TODO: Write magic bytes to identify file format and version.
  dictionaryBuffer[0] = documentMetadata.size();
  dictionaryBuffer[1] = totalUniqWords + 1;

  avengine::vector<char> stringBuffer;
  buildStringTable(dictionaryBuffer, stringBuffer);

  // TODO: maybe fallocate?
  if (lseek(fd, needed * sizeof(uint64_t), SEEK_SET) == (off_t)-1)
    return true;
  if (write(fd, stringBuffer.begin(), stringBuffer.size()) < 0)
    return true;

  size_t postingsOffset = 0;
  avengine::vector<uint8_t> postingBuffer;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      dictionaryBuffer[2 + bucket->hash * 2 + 1] = postingsOffset;

      encodePostingList(*bucket, postingBuffer);

      if (write(fd, postingBuffer.begin(), postingBuffer.size()) < 0)
        return true;
      postingsOffset += postingBuffer.size();

      bucket = bucket->nextBucket;
    }
  }

  if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
    return true;
  if (write(fd, dictionaryBuffer, needed * sizeof(uint64_t)) < 0)
    return true;
  return false;
}

uint32_t Index::beginDocument(DocumentMetadata metadata)
{
  documentMetadata.pushBack(metadata);
  currentDocId = static_cast<uint32_t>(documentMetadata.size() - 1);
  documentStartOffset = offset;
  return currentDocId;
}

void Index::ingest(char const* word)
{
  assert(currentDocId != UINT32_MAX);
  PostingList* bucket = getBucket(word);

  bucket->ingest(currentDocId, offset++);
}

void Index::endDocument()
{
  ingest(DOCEND_TOKEN);
}

Index globalIndex;

// Walks `text`, lowercases and null-terminates each alpha run in-place, and
// ingests each resulting token into `idx`. Returns the number of tokens found.
static size_t tokenizeInto(char* text, Index& idx)
{
  size_t words = 0;

  while (*text && !isalpha(*text))
    text++;

  while (*text)
  {
    char const* word = text;

    while (*text && isalpha(*text))
    {
      *text = *text | 32;
      text++;
    }

    *(text++) = '\0';

    idx.ingest(word);
    words++;

    while (*text && !isalpha(*text))
      text++;
  }

  globalIndex.endDocument();
  return words;
}

// TODO: When string slice is merged we should use that everywhere instead of cstrings.
size_t indexDocument(char const* url, char* phrases)
{
  globalIndex.beginDocument({.url = url, .title = url, .scores = {0}});
  return tokenizeInto(phrases, globalIndex);
}
