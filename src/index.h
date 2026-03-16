#pragma once
#include "avengine/vector.h"
#include <cstddef>
#include <cstdint>

size_t djb2Hash(char const* str);

constexpr uint64_t INDEX_FILE_MAGIC = 0x3256465345584449ULL;
constexpr uint64_t INDEX_FILE_VERSION_V2 = 2;
constexpr uint64_t INDEX_FILE_VERSION_V3 = 3;
constexpr uint64_t INDEX_FILE_FIELD_COUNT = 3;

// TODO: Use support phrases of different kinds and decorate or index titles differently.
enum class PhraseKind
{
  TITLE,
  NORMAL,
  ANCHOR,
};

// Consider switching from AoS to SoA.
struct Posting
{
  uint32_t docId;
  uint32_t normalTermFreq;
  uint32_t titleTermFreq;
  uint32_t anchorTermFreq;
};

// TODO: Determine the best expected hashmap size given a read-world dataset.
#define HASHMAP_SIZE 262144
#define STRATEGIES_COUNT 5
#define STARTING_CAPACITY 64
// A special token used to signify the end of a document. This should NEVER be parsed as a regular
// token.
#define DOCEND_TOKEN "DOCUMENT_END"

struct PostingList
{
  PostingList(char const* word, PostingList* nextBucket);
  Posting* findPosting(uint32_t docId);
  void ingest(uint32_t docId, uint32_t offset, PhraseKind kind);

  char* word;
  size_t hash;
  PostingList* nextBucket;
  // NOTE: On-disk field streams are decorator blocks followed by delta positions.
  avengine::vector<Posting> postings;
  avengine::vector<uint32_t> normalPositions;
  avengine::vector<uint32_t> titlePositions;
  avengine::vector<uint32_t> anchorPositions;
};

// TODO: Determine the best expected hashmap size given a read-world dataset.
#define HASHMAP_SIZE 262144

void append_var_uint32(avengine::vector<uint8_t>& out, uint32_t value);
void append_uint64(avengine::vector<uint8_t>& out, uint64_t value);

struct DocumentMetadata
{
  const char* url;
  const char* title;
  // NOTE: If we choose to add a description for pages it will be passed here.
  int scores[STRATEGIES_COUNT];
};
struct Index
{
  avengine::vector<PostingList*> buckets;
  size_t normalOffset = 0;
  size_t titleOffset = 0;
  size_t anchorOffset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<DocumentMetadata> documentMetadata;
  // Updated by `beginDocument()`. ingest(word) always indexes into this doc.
  uint32_t currentDocId = UINT32_MAX;
  // Offset at the start of the current document, used to compute its term count.
  size_t documentStartOffset = 0;

  PostingList* getBucket(char const* word);
  // Probes dictionaryBuffer for hash slots, interns each term into stringBuffer,
  // and assigns bucket->hash. dict must be zeroed and sized for the full layout.
  void buildStringTable(uint64_t* dict, avengine::vector<char>& stringBuffer);

  Index();
  // Returns true on I/O error.
  bool serialize(int fd);
  // Starts a new active document and returns its assigned id.
  uint32_t beginDocument(DocumentMetadata metadata);
  // Records the term count for the current document and marks it as closed.
  void endDocument();
  // Adds a token to the currently active document set by beginDocument().
  void ingest(char const* word);
  void ingest(char const* word, PhraseKind kind);
  void ingestIntoDocument(uint32_t docId, char const* word, PhraseKind kind);
};

extern Index globalIndex;

// Tokenizes `phrases` (ASCII alpha sequences, lowercased in-place) and ingests
// each token into the active document. `phrases` must be a writable buffer.
// Returns the number of tokens ingested.
size_t indexDocument(char const* url, char* phrases);

struct Phrase
{
  char* text;
  PhraseKind kind;
};
