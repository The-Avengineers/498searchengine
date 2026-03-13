#pragma once
#include "avengine/vector.h"
#include <cstddef>
#include <cstdint>

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
  // NOTE: When we figure out exactly what is needed for the skip pointers and synchronization
  // points in the on-disk-format we should update this to match.
  avengine::vector<Posting> postings;
  avengine::vector<uint32_t> positions;
};

// TODO: Determine the best expected hashmap size given a read-world dataset.
#define HASHMAP_SIZE 262144

void append_var_uint32(avengine::vector<uint8_t>& out, uint32_t value);
void append_uint64(avengine::vector<uint8_t>& out, uint64_t value);

struct Index
{
  avengine::vector<PostingList*> buckets;
  size_t offset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<const char*> documentNames;
  // Updated by beginDocument(). ingest(word) always indexes into this doc.
  uint32_t currentDocId = UINT32_MAX;

  PostingList* getBucket(char const* word);
  // Probes dictionaryBuffer for hash slots, interns each term into stringBuffer,
  // and assigns bucket->hash. dict must be zeroed and sized for the full layout.
  void buildStringTable(uint64_t* dict, avengine::vector<char>& stringBuffer);

  Index();
  // Returns true on I/O error.
  bool serialize(int fd);
  // Starts a new active document and returns its assigned id.
  uint32_t beginDocument(const char* url);
  // Adds a token to the currently active document set by beginDocument().
  void ingest(char const* word);
};

extern Index globalIndex;

// Tokenizes `phrases` (ASCII alpha sequences, lowercased in-place) and ingests
// each token into the active document. `phrases` must be a writable buffer.
// Returns the number of tokens ingested.
size_t indexDocument(char const* url, char* phrases);

// TODO: Use support phrases of different kinds and decorate or index titles differently.
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
