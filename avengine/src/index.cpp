#include "index.h"
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace
{
struct SkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

struct DictEntry
{
  uint64_t stringOffset;
  uint64_t postingOffset;
};

static void append_var_uint32(avengine::vector<uint8_t>& out, uint32_t value)
{
  while (value >= 0x80u)
  {
    out.pushBack(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  out.pushBack(static_cast<uint8_t>(value));
}

static bool seek_to(int fd, off_t offset)
{
  return lseek(fd, offset, SEEK_SET) >= 0;
}

static bool write_all(int fd, const void* buffer, size_t bytes)
{
  const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
  size_t left = bytes;

  while (left > 0)
  {
    ssize_t written = write(fd, ptr, left);
    if (written <= 0)
      return false;
    ptr += static_cast<size_t>(written);
    left -= static_cast<size_t>(written);
  }

  return true;
}

static void encode_field(const avengine::vector<uint32_t>& positions,
                         avengine::vector<uint8_t>& deltaBuffer,
                         avengine::vector<uint8_t>& skipBuffer,
                         avengine::vector<SkipPointer>& skipPointers)
{
  deltaBuffer.clear();
  skipBuffer.clear();
  skipPointers.clear();

  uint32_t lastPosition = 0;
  const uint32_t collectionFrequency = static_cast<uint32_t>(positions.size());
  const uint32_t skipStride =
      collectionFrequency > 0 ? static_cast<uint32_t>(std::sqrt(collectionFrequency)) : 0;

  for (uint32_t i = 0; i < collectionFrequency; i++)
  {
    const uint32_t position = positions[i];
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
}
} // namespace

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

Posting* PostingList::findPosting(uint32_t docId)
{
  for (size_t i = 0; i < postings.size(); i++)
  {
    if (postings[i].docId == docId)
      return &postings[i];
  }

  return nullptr;
}

void PostingList::ingest(uint32_t docId, uint32_t offset, PhraseKind kind)
{
  Posting* posting = findPosting(docId);
  if (!posting)
  {
    postings.pushBack(
        {.docId = docId, .normalTermFreq = 0, .titleTermFreq = 0, .anchorTermFreq = 0});
    posting = &postings.back();
  }

  switch (kind)
  {
  case PhraseKind::NORMAL:
    posting->normalTermFreq++;
    normalPositions.pushBack(offset);
    break;
  case PhraseKind::TITLE:
    posting->titleTermFreq++;
    titlePositions.pushBack(offset);
    break;
  case PhraseKind::ANCHOR:
    posting->anchorTermFreq++;
    anchorPositions.pushBack(offset);
    break;
  }
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
  const uint64_t uniqueWordsPlusOne = totalUniqWords + 1;
  const uint64_t tableSlots = uniqueWordsPlusOne * 2;
  const size_t dictionaryBytes = static_cast<size_t>(tableSlots) * sizeof(DictEntry);

  DictEntry* dictionary = new DictEntry[tableSlots];
  for (uint64_t i = 0; i < tableSlots; i++)
  {
    dictionary[i].stringOffset = UINT64_MAX;
    dictionary[i].postingOffset = UINT64_MAX;
  }

  avengine::vector<char> stringBuffer;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      uint64_t hash = djb2Hash(bucket->word) % tableSlots;

      while (dictionary[hash].stringOffset != UINT64_MAX)
      {
        hash += 1;
        hash %= tableSlots;
      }

      dictionary[hash].stringOffset = static_cast<uint64_t>(stringBuffer.size());

      for (char const* str = bucket->word; *str; str++)
        stringBuffer.pushBack(*str);
      stringBuffer.pushBack('\0');

      bucket->hash = hash;
      bucket = bucket->nextBucket;
    }
  }

  const size_t headerBytes = sizeof(uint64_t) * 5;
  const size_t postingsStart = headerBytes + dictionaryBytes + stringBuffer.size();

  if (!seek_to(fd, static_cast<off_t>(postingsStart)))
  {
    delete[] dictionary;
    return;
  }

  size_t postingsOffset = 0;
  avengine::vector<uint8_t> normalDeltaBuffer;
  avengine::vector<uint8_t> normalSkipBuffer;
  avengine::vector<SkipPointer> normalSkips;

  avengine::vector<uint8_t> titleDeltaBuffer;
  avengine::vector<uint8_t> titleSkipBuffer;
  avengine::vector<SkipPointer> titleSkips;

  avengine::vector<uint8_t> anchorDeltaBuffer;
  avengine::vector<uint8_t> anchorSkipBuffer;
  avengine::vector<SkipPointer> anchorSkips;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];

    while (bucket)
    {
      const size_t hash = bucket->hash;
      dictionary[hash].postingOffset = static_cast<uint64_t>(postingsOffset);

      encode_field(bucket->normalPositions, normalDeltaBuffer, normalSkipBuffer, normalSkips);
      encode_field(bucket->titlePositions, titleDeltaBuffer, titleSkipBuffer, titleSkips);
      encode_field(bucket->anchorPositions, anchorDeltaBuffer, anchorSkipBuffer, anchorSkips);

      const uint64_t postingHeader[] = {
          static_cast<uint64_t>(bucket->postings.size()),
          static_cast<uint64_t>(bucket->normalPositions.size()),
          static_cast<uint64_t>(bucket->titlePositions.size()),
          static_cast<uint64_t>(bucket->anchorPositions.size()),
          static_cast<uint64_t>(normalSkips.size()),
          static_cast<uint64_t>(titleSkips.size()),
          static_cast<uint64_t>(anchorSkips.size()),
      };

      if (!write_all(fd, postingHeader, sizeof(postingHeader)))
      {
        delete[] dictionary;
        return;
      }

      if (!normalSkipBuffer.empty() &&
          !write_all(fd, normalSkipBuffer.begin(), normalSkipBuffer.size()))
      {
        delete[] dictionary;
        return;
      }
      if (!normalDeltaBuffer.empty() &&
          !write_all(fd, normalDeltaBuffer.begin(), normalDeltaBuffer.size()))
      {
        delete[] dictionary;
        return;
      }

      if (!titleSkipBuffer.empty() &&
          !write_all(fd, titleSkipBuffer.begin(), titleSkipBuffer.size()))
      {
        delete[] dictionary;
        return;
      }
      if (!titleDeltaBuffer.empty() &&
          !write_all(fd, titleDeltaBuffer.begin(), titleDeltaBuffer.size()))
      {
        delete[] dictionary;
        return;
      }

      if (!anchorSkipBuffer.empty() &&
          !write_all(fd, anchorSkipBuffer.begin(), anchorSkipBuffer.size()))
      {
        delete[] dictionary;
        return;
      }
      if (!anchorDeltaBuffer.empty() &&
          !write_all(fd, anchorDeltaBuffer.begin(), anchorDeltaBuffer.size()))
      {
        delete[] dictionary;
        return;
      }

      postingsOffset += sizeof(postingHeader);
      postingsOffset += normalSkipBuffer.size() + normalDeltaBuffer.size();
      postingsOffset += titleSkipBuffer.size() + titleDeltaBuffer.size();
      postingsOffset += anchorSkipBuffer.size() + anchorDeltaBuffer.size();

      bucket = bucket->nextBucket;
    }
  }

  if (!seek_to(fd, 0))
  {
    delete[] dictionary;
    return;
  }

  const uint64_t fileHeader[] = {INDEX_FILE_MAGIC_V2, INDEX_FILE_VERSION_V2,
                                 static_cast<uint64_t>(documentNames.size()), uniqueWordsPlusOne,
                                 INDEX_FILE_FIELD_COUNT_V2};

  if (!write_all(fd, fileHeader, sizeof(fileHeader)))
  {
    delete[] dictionary;
    return;
  }

  if (!write_all(fd, dictionary, dictionaryBytes))
  {
    delete[] dictionary;
    return;
  }

  if (!stringBuffer.empty() && !write_all(fd, stringBuffer.begin(), stringBuffer.size()))
  {
    delete[] dictionary;
    return;
  }

  delete[] dictionary;
}

uint32_t Index::beginDocument(const char* url)
{
  documentNames.pushBack(url);
  currentDocId = static_cast<uint32_t>(documentNames.size() - 1);
  return currentDocId;
}

void Index::ingest(char const* word)
{
  ingest(word, PhraseKind::NORMAL);
}

void Index::ingest(char const* word, PhraseKind kind)
{
  assert(currentDocId != UINT32_MAX);
  ingestIntoDocument(currentDocId, word, kind);
}

void Index::ingestIntoDocument(uint32_t docId, char const* word, PhraseKind kind)
{
  PostingList* bucket = getBucket(word);

  switch (kind)
  {
  case PhraseKind::NORMAL:
    bucket->ingest(docId, static_cast<uint32_t>(normalOffset++), kind);
    break;
  case PhraseKind::TITLE:
    bucket->ingest(docId, static_cast<uint32_t>(titleOffset++), kind);
    break;
  case PhraseKind::ANCHOR:
    bucket->ingest(docId, static_cast<uint32_t>(anchorOffset++), kind);
    break;
  }
}

Index globalIndex;

size_t indexDocument(char const* url, char* pharses)
{
  globalIndex.beginDocument(url);
  size_t words = 0;

  while (*pharses && !std::isalpha(static_cast<unsigned char>(*pharses)))
    pharses++;

  while (*pharses)
  {
    char const* word = pharses;

    while (*pharses && std::isalpha(static_cast<unsigned char>(*pharses)))
    {
      *pharses = static_cast<char>(std::tolower(static_cast<unsigned char>(*pharses)));
      pharses++;
    }

    *(pharses++) = '\0';

    globalIndex.ingest(word, PhraseKind::NORMAL);
    words++;

    while (*pharses && !std::isalpha(static_cast<unsigned char>(*pharses)))
      pharses++;
  }

  return words;
}
