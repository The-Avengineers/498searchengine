#pragma once

#include "avengine/string.h"
#include "avengine/vector.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

size_t djb2Hash(char const* str);

constexpr uint64_t INDEX_FILE_MAGIC = 0x3256465345584449ULL;
constexpr uint64_t INDEX_FILE_VERSION = 4;

enum class PhraseKind
{
  BODY,
  TITLE,
  URL,
};

struct Posting
{
  uint32_t docId = 0;
  uint32_t termFreq = 0;
};

#define HASHMAP_SIZE 262144
#define STARTING_CAPACITY 64

struct PostingList
{
  PostingList(char const* word, PostingList* nextBucket);
  Posting* findPosting(uint32_t docId);
  void ingest(uint32_t docId, uint32_t offset);

  char* word = nullptr;
  size_t hash = 0;
  PostingList* nextBucket = nullptr;
  avengine::vector<Posting> postings;
  avengine::vector<uint32_t> positions;
};

void append_var_uint32(avengine::vector<uint8_t>& out, uint32_t value);
void append_uint64(avengine::vector<uint8_t>& out, uint64_t value);

struct Index
{
  avengine::vector<PostingList*> buckets;
  size_t offset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<avengine::string> documentNames;
  pthread_mutex_t writeMutex;

  PostingList* getBucket(char const* word);

  Index();
  ~Index();
  bool serialize(int fd);
  uint32_t beginDocument(const char* url);
  void ingestIntoDocument(uint32_t docId, char const* word, PhraseKind kind);
};

extern Index globalIndex;

size_t indexDocument(char const* url, char* phrases);

struct Phrase
{
  char* text;
  PhraseKind kind;
};
