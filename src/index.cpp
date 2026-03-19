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
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

size_t djb2Hash(avengine::string_slice str)
{
  size_t hash = 5381;

  for (size_t i = 0; i < str.size(); i++)
    hash = ((hash << 5) + hash) + static_cast<unsigned char>(str[i]); /* hash * 33 + c */

  return hash;
}

PostingList::PostingList(avengine::string_slice word, PostingList* nextBucket)
    : nextBucket(nextBucket)
{
  this->word = word.toString();
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

static void append_string(avengine::vector<uint8_t>& out, const avengine::string& str)
{
  const size_t len = str.size();
  append_var_uint32(out, static_cast<uint32_t>(len));
  append_bytes(out, str.begin(), len);
}

struct SkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

PostingList* Index::getBucket(avengine::string_slice word)
{
  PostingList** bucket = &buckets[djb2Hash(word) % HASHMAP_SIZE];

  while (*bucket && avengine::string_slice((*bucket)->word) != word)
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
  pthread_mutex_init(&writeMutex, nullptr);
}

Index::~Index()
{
  pthread_mutex_destroy(&writeMutex);
}

// Encodes a single posting list into `out` (cleared on entry):
//   [doc_freq u64][collection_freq u64][skip_count u64]
//   [skip stream varint*][position delta stream varint*]
static void encodePostingList(const PostingList& pl,
                              const avengine::vector<DocumentMetadata>& docMetadata,
                              avengine::vector<uint8_t>& out)
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

  // For DOCUMENT_END, serialize document metadata inline in posting order.
  avengine::vector<uint8_t> metadataBuffer;
  if (pl.word == avengine::string(DOCEND_TOKEN))
  {
    // DOCUMENT_END stores one entry per doc. Its position deltas encode term counts,
    // and document metadata is serialized inline in posting order.
    for (const Posting& posting : pl.postings)
    {
      assert(posting.docId < docMetadata.size());
      const DocumentMetadata& doc = docMetadata[posting.docId];

      append_string(metadataBuffer, doc.url);
      append_string(metadataBuffer, doc.title);
      append_bytes(metadataBuffer, reinterpret_cast<const uint8_t*>(&doc.input), sizeof(doc.input));
    }
  }

  // Header: [doc_frequency u64][collection_frequency u64][skip_count u64].
  out.clear();
  append_uint64(out, pl.postings.size());
  append_uint64(out, collectionFrequency);
  append_uint64(out, skipPointers.size());
  append_bytes(out, skipBuffer.begin(), skipBuffer.size());
  append_bytes(out, deltaBuffer.begin(), deltaBuffer.size());
  append_bytes(out, metadataBuffer.begin(), metadataBuffer.size());
}

void Index::buildStringTable(uint64_t* dict, avengine::vector<char>& stringBuffer)
{
  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      avengine::string_slice wordSlice(bucket->word);
      size_t hash = djb2Hash(wordSlice) % (totalUniqWords * 2);

      // Linear probe for an empty slot.
      // TODO: Benchmark alternative probing strategies when querying the index.
      while (dict[2 + hash * 2] > 0)
      {
        hash += 1;
        hash %= (totalUniqWords * 2);
      }

      dict[2 + hash * 2] = stringBuffer.size();

      for (size_t i = 0; i < bucket->word.size(); i++)
        stringBuffer.pushBack(bucket->word[i]);
      stringBuffer.pushBack('\0');

      bucket->hash = hash;
      bucket = bucket->nextBucket;
    }
  }
}

bool Index::serialize(int fd)
{
  pthread_mutex_lock(&writeMutex);

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
  {
    pthread_mutex_unlock(&writeMutex);
    return true;
  }
  if (write(fd, stringBuffer.begin(), stringBuffer.size()) < 0)
  {
    pthread_mutex_unlock(&writeMutex);
    return true;
  }

  size_t postingsOffset = 0;
  avengine::vector<uint8_t> postingBuffer;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      dictionaryBuffer[2 + bucket->hash * 2 + 1] = postingsOffset;

      encodePostingList(*bucket, documentMetadata, postingBuffer);

      if (write(fd, postingBuffer.begin(), postingBuffer.size()) < 0)
      {
        pthread_mutex_unlock(&writeMutex);
        return true;
      }
      postingsOffset += postingBuffer.size();

      bucket = bucket->nextBucket;
    }
  }

  if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
  {
    pthread_mutex_unlock(&writeMutex);
    return true;
  }
  if (write(fd, dictionaryBuffer, needed * sizeof(uint64_t)) < 0)
  {
    pthread_mutex_unlock(&writeMutex);
    return true;
  }

  pthread_mutex_unlock(&writeMutex);
  return false;
}

uint32_t Index::beginDocument(DocumentMetadata metadata)
{
  pthread_mutex_lock(&writeMutex);
  documentMetadata.pushBack(metadata);
  uint32_t docId = static_cast<uint32_t>(documentMetadata.size() - 1);
  pthread_mutex_unlock(&writeMutex);
  return docId;
}

void Index::ingestIntoDocument(uint32_t docId, avengine::string_slice word)
{
  pthread_mutex_lock(&writeMutex);
  PostingList* bucket = getBucket(word);
  bucket->ingest(docId, offset++);
  pthread_mutex_unlock(&writeMutex);
}

void Index::endDocument(uint32_t docId)
{
  ingestIntoDocument(docId, DOCEND_TOKEN);
}

Index globalIndex;

// Walks `text`, lowercases each alpha run in-place via the mutable slice, and
// ingests each resulting token into `idx` under `docId`. Returns the number of
// tokens found.
static size_t tokenizeInto(avengine::string_slice text, Index& idx, uint32_t docId)
{
  size_t words = 0;
  size_t i = 0;

  while (i < text.size() && !isalpha(text[i]))
    i++;

  while (i < text.size())
  {
    size_t wordStart = i;

    while (i < text.size() && isalpha(text[i]))
    {
      text[i] = text[i] | 32;
      i++;
    }

    idx.ingestIntoDocument(docId, text.substr(wordStart, i - wordStart));
    words++;

    while (i < text.size() && !isalpha(text[i]))
      i++;
  }

  idx.endDocument(docId);
  return words;
}

size_t indexDocument(avengine::string_slice url, avengine::string_slice phrases)
{
  uint32_t docId =
      globalIndex.beginDocument({.url = url.toString(), .title = url.toString(), .input = 0});
  return tokenizeInto(phrases, globalIndex, docId);
}
