#pragma once
#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/string_slice.h"
#include "avengine/vector.h"
#include <cstddef>
#include <cstdint>

size_t djb2Hash(avengine::string_slice str);

// Consider switching from AoS to SoA.
struct Posting
{
  uint32_t docId;
  uint32_t termFreq;
  // It might make sense to make the just an index if resizing is often changing
  // the base pointer location.
  size_t positions;
};

// TODO: Determine the best expected hashmap size given a read-world dataset.
#define HASHMAP_SIZE 262144
#define STARTING_CAPACITY 64
// A special token used to signify the end of a document. This should NEVER be parsed as a regular
// token.
constexpr const char* DOCEND_TOKEN = "DOCUMENT_END";

// The raw data needed as input to calculate the static rank (distance from seed list, length of
// document, etc). It MUST be plain old data, no pointers.
using RankingInput = uint64_t;

struct PostingList
{
  PostingList(avengine::string_slice word, PostingList* nextBucket);
  void ingest(uint32_t docId, size_t offset);

  avengine::string word;
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

struct DocumentMetadata
{
  avengine::string url;
  avengine::string title;
  // NOTE: If we choose to add a description for pages it will be passed here.
  RankingInput input;
};
struct Index
{
  avengine::vector<PostingList*> buckets;
  size_t offset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<DocumentMetadata> documentMetadata;
  // Updated by `beginDocument()`. ingest(word) always indexes into this doc.
  uint32_t currentDocId = UINT32_MAX;

  PostingList* getBucket(avengine::string_slice word);
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
  void ingest(avengine::string_slice word);
};

extern Index globalIndex;

// Tokenizes `phrases` (ASCII alpha sequences, lowercased in-place) and ingests
// each token into the active document. `phrases` must be a writable buffer.
// Returns the number of tokens ingested.
size_t indexDocument(avengine::string_slice url, avengine::string_slice phrases);

// TODO: Use support phrases of different kinds and decorate or index titles differently.
enum class PhraseKind
{
  TITLE,
  NORMAL,
};

struct Phrase
{
  avengine::string_slice text;
  PhraseKind kind;
};
