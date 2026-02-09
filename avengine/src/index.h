#pragma once
#include "avengine/vector.h"
#include <cstddef>
#include <cstdint>

void parseKjvVerses(char* content, avengine::vector<char*>& documents,
                    avengine::vector<char*>& documentNames);

char* memoryMapFile(char const* filename, size_t& length);

size_t djb2Hash(char const* str);

// Consider switching from AoS to SoA.
struct Posting
{
  uint32_t docId;
  uint32_t termFreq;
  // It might make sense to make the just an index if resizing is often changing
  // the base pointer location.
  size_t positions;
};

#define STARTING_CAPACITY 64

struct PostingList
{
  PostingList(char const* word, PostingList* nextBucket);
  void ingest(uint32_t docId, size_t offset);

  char* word;
  size_t hash;
  PostingList* nextBucket;
  avengine::vector<Posting> postings;
  avengine::vector<uint32_t> positions;
};

#define HASHMAP_SIZE 262144

// TODO: Actually write a variable length integers instead
uint8_t* write_var_uint32(uint8_t* buffer, uint32_t value);
uint8_t* write_uint64(uint8_t* buffer, uint64_t value);

struct Index
{
  avengine::vector<PostingList*> buckets;
  size_t offset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<const char*> documentNames;
  // Updated by beginDocument(). ingest(word) always indexes into this doc.
  uint32_t currentDocId = UINT32_MAX;

  PostingList* getBucket(char const* word);

  Index();
  void serialize(int fd);
  // Starts a new active document and returns its assigned id.
  uint32_t beginDocument(const char* url);
  // Adds a token to the currently active document set by beginDocument().
  void ingest(char const* word);
};

extern Index globalIndex;

// Puts the list of words into the currently active index. url must in it's
// canonical form. Words is a list of pointers to cstrings in a single
// continuous block of memory. The text in pharses must be writable and
// monotonically increasing, i.e phrases[i].text < phrases.text[i + 1].
size_t indexDocument(char const* url, char* pharses);

enum class PhraseKind
{
  TITLE,
  NORMAL,
};

struct Phrase
{
  char* text;
  PhraseKind kind;
};
