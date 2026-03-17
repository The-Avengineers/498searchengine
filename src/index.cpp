#include "index.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

namespace
{
struct DictEntry
{
  uint64_t stringOffset;
  uint64_t postingOffset;
};

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

static void encode_positions(const avengine::vector<uint32_t>& positions,
                             avengine::vector<uint8_t>& deltaBuffer)
{
  deltaBuffer.clear();

  uint32_t lastPosition = 0;
  for (size_t i = 0; i < positions.size(); i++)
  {
    const uint32_t position = positions[i];
    append_var_uint32(deltaBuffer, position - lastPosition);
    lastPosition = position;
  }
}

static avengine::string decorate_term(char const* word, PhraseKind kind)
{
  avengine::string decorated;

  if (kind == PhraseKind::TITLE)
    decorated.pushBack('#');
  else if (kind == PhraseKind::URL)
    decorated.pushBack('/');

  for (const char* p = word; *p; ++p)
    decorated.pushBack(*p);

  return decorated;
}
} // namespace

size_t djb2Hash(char const* str)
{
  size_t hash = 5381;
  int c = 0;

  while ((c = *str++) != 0)
    hash = ((hash << 5) + hash) + static_cast<size_t>(c);

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

void PostingList::ingest(uint32_t docId, uint32_t offset)
{
  Posting* posting = findPosting(docId);
  if (!posting)
  {
    postings.pushBack({.docId = docId, .termFreq = 0});
    posting = &postings.back();
  }

  posting->termFreq++;
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
    out.pushBack(bytes[i]);
}

void append_uint64(avengine::vector<uint8_t>& out, uint64_t value)
{
  append_bytes(out, &value, sizeof(uint64_t));
}

PostingList* Index::getBucket(char const* word)
{
  PostingList** bucket = &buckets[djb2Hash(word) % HASHMAP_SIZE];

  while (*bucket && strcmp((*bucket)->word, word) != 0)
    bucket = &(*bucket)->nextBucket;

  if (!*bucket)
  {
    totalUniqWords++;
    *bucket = new PostingList(word, nullptr);
  }

  return *bucket;
}

Index::Index() : buckets(HASHMAP_SIZE)
{
  for (size_t i = 0; i < buckets.size(); i++)
    buckets[i] = nullptr;
}

bool Index::serialize(int fd)
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
      for (const char* str = bucket->word; *str; ++str)
        stringBuffer.pushBack(*str);
      stringBuffer.pushBack('\0');

      bucket->hash = static_cast<size_t>(hash);
      bucket = bucket->nextBucket;
    }
  }

  const size_t headerBytes = sizeof(uint64_t) * 4;
  const size_t postingsStart = headerBytes + dictionaryBytes + stringBuffer.size();
  if (!seek_to(fd, static_cast<off_t>(postingsStart)))
  {
    delete[] dictionary;
    return true;
  }

  size_t postingsOffset = 0;
  avengine::vector<uint8_t> deltaBuffer;

  for (size_t i = 0; i < HASHMAP_SIZE; i++)
  {
    PostingList* bucket = buckets[i];
    while (bucket)
    {
      dictionary[bucket->hash].postingOffset = static_cast<uint64_t>(postingsOffset);
      encode_positions(bucket->positions, deltaBuffer);

      const uint64_t postingHeader[] = {
          static_cast<uint64_t>(bucket->postings.size()),
          static_cast<uint64_t>(bucket->positions.size()),
      };

      if (!write_all(fd, postingHeader, sizeof(postingHeader)))
      {
        delete[] dictionary;
        return true;
      }

      if (!deltaBuffer.empty() && !write_all(fd, deltaBuffer.begin(), deltaBuffer.size()))
      {
        delete[] dictionary;
        return true;
      }

      postingsOffset += sizeof(postingHeader) + deltaBuffer.size();
      bucket = bucket->nextBucket;
    }
  }

  if (!seek_to(fd, 0))
  {
    delete[] dictionary;
    return true;
  }

  const uint64_t fileHeader[] = {INDEX_FILE_MAGIC, INDEX_FILE_VERSION,
                                 static_cast<uint64_t>(documentNames.size()), uniqueWordsPlusOne};

  if (!write_all(fd, fileHeader, sizeof(fileHeader)) ||
      !write_all(fd, dictionary, dictionaryBytes) ||
      (!stringBuffer.empty() && !write_all(fd, stringBuffer.begin(), stringBuffer.size())))
  {
    delete[] dictionary;
    return true;
  }

  delete[] dictionary;
  return false;
}

uint32_t Index::beginDocument(const char* url)
{
  documentNames.pushBack(avengine::string(url));
  currentDocId = static_cast<uint32_t>(documentNames.size() - 1);
  return currentDocId;
}

void Index::ingest(char const* word)
{
  ingest(word, PhraseKind::BODY);
}

void Index::ingest(char const* word, PhraseKind kind)
{
  assert(currentDocId != UINT32_MAX);
  ingestIntoDocument(currentDocId, word, kind);
}

void Index::ingestIntoDocument(uint32_t docId, char const* word, PhraseKind kind)
{
  const avengine::string decorated = decorate_term(word, kind);
  PostingList* bucket = getBucket(decorated.cstr());
  bucket->ingest(docId, static_cast<uint32_t>(offset++));
}

Index globalIndex;

size_t indexDocument(char const* url, char* phrases)
{
  globalIndex.beginDocument(url);
  size_t words = 0;

  while (*phrases && !isalpha(static_cast<unsigned char>(*phrases)))
    phrases++;

  while (*phrases)
  {
    char const* word = phrases;

    while (*phrases && isalpha(static_cast<unsigned char>(*phrases)))
    {
      *phrases = static_cast<char>(tolower(static_cast<unsigned char>(*phrases)));
      phrases++;
    }

    *(phrases++) = '\0';

    globalIndex.ingest(word, PhraseKind::BODY);
    words++;

    while (*phrases && !isalpha(static_cast<unsigned char>(*phrases)))
      phrases++;
  }

  return words;
}
