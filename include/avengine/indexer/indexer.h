#pragma once
#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/string_slice.h"
#include "avengine/codec.h"
#include "avengine/indexer/ranking_input.h"
#include "avengine/unordered_map.h"
#include "avengine/vector.h"
#include <cstddef>
#include <cstdint>
#include <pthread.h>

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

// A special token used to signify the end of a document. This should NEVER be parsed as a regular
// token.
constexpr const char* DOCEND_TOKEN = "DOCUMENT_END";
constexpr const size_t OFFSET_THRESHOLD = 1 << 25;

constexpr const char* INDEX_DIRECTORY = ".";

// Number of independent index slots for concurrent document ingestion.
constexpr size_t INDEX_SLOT_COUNT = 8;

// The raw data needed as input to calculate the static rank (distance from seed list, length of
// document, etc). It MUST be plain old data, no pointers.
using RankingInput = uint64_t;


struct PostingList
{
  PostingList() = default;
  PostingList(avengine::string_slice word);
  void ingest(uint32_t docId, size_t offset);

  avengine::string word;
  size_t hash = 0;
  // NOTE: When we figure out exactly what is needed for the skip pointers and synchronization
  // points in the on-disk-format we should update this to match.
  avengine::vector<Posting> postings;
  avengine::vector<uint32_t> positions;
};

struct DocumentMetadata
{
  avengine::string url;
  avengine::string title;
  // NOTE: If we choose to add a description for pages it will be passed here.
  RankingInput input;
};
struct Index
{
  avengine::unordered_map<PostingList> map;
  size_t offset = 0;
  size_t totalUniqWords = 0;
  avengine::vector<DocumentMetadata> documentMetadata;
  uint32_t currentDocId = 0;

  PostingList* getBucket(avengine::string_slice word);
  size_t headerSize();

  Index();
  ~Index();
  // Returns true on I/O error.
  bool serialize(int fd);
  // Takes the write lock and allocates a doc slot. The lock is held until
  // endDocument() is called. Returns the assigned docId.
  uint32_t beginDocument(DocumentMetadata metadata);
  // Ingests the DOCEND_TOKEN and releases the write lock.
  void endDocument();
  // Adds a token to the current document. Must be called between beginDocument()
  // and endDocument().
  void ingest(avengine::string_slice word);
};

struct IndexSlot
{
  Index* index;
  pthread_mutex_t mutex;
};

extern IndexSlot globalIndexSlots[INDEX_SLOT_COUNT];

void* serializeIndex(Index* index);

void cleanUpIndexer();
void resumeIndexer();

enum class PhraseKind
{
  BODY,
  TITLE,
};

size_t tokenizeInto(avengine::vector<avengine::string_slice>& texts, Index& idx, PhraseKind kind);

// Tokenizes `phrases` (ASCII alpha sequences, lowercased in-place) and ingests
// each token into the active document. `phrases` must be a writable buffer.
// Title terms are copied from document metadata and stored with a '#' prefix.
// Returns the number of tokens ingested.
size_t indexDocument(DocumentMetadata& document_metadata,
                     avengine::vector<avengine::string_slice>& phrases);
