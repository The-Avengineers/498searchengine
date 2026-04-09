#include <atomic>
#include <avengine/avengine_lib/vector.h>
#include <avengine/codec.h>
#include <avengine/indexer/anchor_indexer.h>
#include <avengine/indexer/indexer.h>
#include <avengine/indexer/token_rules.h>
#include <cassert>
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

PostingList::PostingList(avengine::string_slice word)
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

struct SkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

PostingList* Index::getBucket(avengine::string_slice word)
{
  PostingList* pl = map.get(word);

  if (!pl)
  {
    map.insert(word.toString(), PostingList(word));
    totalUniqWords++;
    // Re-fetch after insert: a rehash may have moved internal bucket array
    // slots, but each heap-allocated Bucket node (and the PostingList stored
    // within it) is never relocated, so the returned pointer is stable.
    pl = map.get(word);
  }

  return pl;
}

Index::Index() {}

Index::~Index() {}

// Encodes a single posting list into `out` (cleared on entry):
//   [doc_freq u64][collection_freq u64][skip_count u64]
//   [skip stream varint*][position delta stream varint*]
static void encodePostingList(const PostingList& pl,
                              const avengine::vector<DocumentMetadata>& docMetadata,
                              avengine::vector<uint8_t>& out)
{
  avengine::Writer deltaWriter;
  avengine::vector<SkipPointer> skipPointers;
  avengine::Writer skipWriter;

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
                             .byteOffset = static_cast<uint32_t>(deltaWriter.size())});
    }
    deltaWriter.writeVarU32(position - lastPosition);
    lastPosition = position;
  }

  for (const SkipPointer& skip : skipPointers)
  {
    skipWriter.writeVarU32(skip.positionIndex);
    skipWriter.writeVarU32(skip.positionValue);
    skipWriter.writeVarU32(skip.byteOffset);
  }

  // For DOCUMENT_END, serialize document metadata inline in posting order.
  avengine::Writer metaWriter;
  if (pl.word == avengine::string(DOCEND_TOKEN))
  {
    // DOCUMENT_END stores one entry per doc. Its position deltas encode term counts,
    // and document metadata is serialized inline in posting order.
    for (const Posting& posting : pl.postings)
    {
      assert(posting.docId < docMetadata.size());
      const DocumentMetadata& doc = docMetadata[posting.docId];

      metaWriter.writeString(doc.url);
      metaWriter.writeString(doc.title);
      metaWriter.writeString(doc.description);
      metaWriter.writeBytes(reinterpret_cast<const uint8_t*>(&doc.input), sizeof(doc.input));
    }
  }

  // Header: [doc_frequency u64][collection_frequency u64][skip_count u64].
  avengine::Writer outWriter;
  outWriter.writeU64(pl.postings.size());
  outWriter.writeU64(collectionFrequency);
  outWriter.writeU64(skipPointers.size());
  outWriter.writeBytes(skipWriter.data(), skipWriter.size());
  outWriter.writeBytes(deltaWriter.data(), deltaWriter.size());
  outWriter.writeBytes(metaWriter.data(), metaWriter.size());
  out = outWriter.release();
}

size_t Index::headerSize()
{
  size_t stringsSize = 0;

  map.forEach(
      [&](const avengine::string& key, PostingList& pl)
      {
        assert(key == pl.word);
        stringsSize += 4 * sizeof(uint64_t);
        stringsSize += key.size();
      });

  return 5 * sizeof(uint64_t) + stringsSize;
}

bool Index::serialize(int fd)
{
  avengine::Writer header;

  header.writeU64(0x8FB8EF84979FF0);
  header.writeU64(documentMetadata.size());
  header.writeU64(totalUniqWords);
  header.writeU64(offset);
  header.writeU64(sizeof(DocumentMetadata::input));

  // TODO: maybe fallocate?
  if (lseek(fd, headerSize(), SEEK_SET) == (off_t)-1)
    return true;

  size_t postingsOffset = 0;
  avengine::vector<uint8_t> postingBuffer;
  bool ioError = false;

  map.forEach(
      [&](const avengine::string& /*key*/, PostingList& pl)
      {
        if (ioError)
          return;

        header.writeString(pl.word);
        header.writeU64(pl.positions.size());
        header.writeU64(pl.postings.size());
        header.writeU64(postingsOffset);

        encodePostingList(pl, documentMetadata, postingBuffer);

        if (write(fd, postingBuffer.begin(), postingBuffer.size()) < 0)
        {
          ioError = true;
          return;
        }
        postingsOffset += postingBuffer.size();
      });

  if (ioError)
    return true;

  if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
    return true;

  if (write(fd, header.data(), header.size()) < 0)
    return true;

  return false;
}

uint32_t Index::beginDocument(DocumentMetadata metadata)
{
  documentMetadata.pushBack(metadata);
  currentDocId = static_cast<uint32_t>(documentMetadata.size() - 1);
  return currentDocId;
}

void Index::ingest(avengine::string_slice word)
{
  PostingList* bucket = getBucket(word);
  bucket->ingest(currentDocId, offset++);
}

void Index::endDocument()
{
  ingest(DOCEND_TOKEN);
}

// Zero-initialise first so the array exists as a global, then the static
// SlotArrayInit constructor fills each element before main() runs.
IndexSlot globalIndexSlots[INDEX_SLOT_COUNT];

namespace
{
// Initialises every slot exactly once, at program startup (before main).
struct SlotArrayInit
{
  SlotArrayInit()
  {
    for (size_t i = 0; i < INDEX_SLOT_COUNT; i++)
    {
      globalIndexSlots[i].index = new Index();
      pthread_mutex_init(&globalIndexSlots[i].mutex, nullptr);
    }
  }
} g_slotArrayInit;
} // namespace

// Shared .sfi filename counter — atomically incremented on every flush.
static std::atomic<size_t> indexCounter{0};

// Round-robin slot selector.
static std::atomic<size_t> g_nextSlot{0};

static avengine::vector<pthread_t> g_serializeThreads;
static pthread_mutex_t g_serializeThreadsMutex = PTHREAD_MUTEX_INITIALIZER;

// Walks `texts`, normalizes each token, and ingests each result into `idx`
// with the given field decorator. Non-alphanumeric characters break words,
// except apostrophes and selected common hyphenated prefixes (e.g. re-use,
// non-profit), which are joined into a single normalized token.
// Tokens with more than 4 total digits are dropped.
// BODY terms are ingested as-is; TITLE terms get a '#' prefix.
// Must be called between beginDocument() and endDocument().
// Returns the number of tokens ingested.
size_t tokenizeInto(avengine::vector<avengine::string_slice>& texts, Index& idx, PhraseKind kind)
{
  size_t words = 0;

  for (auto text : texts)
  {
    size_t i = 0;

    while (i < text.size())
    {
      while (i < text.size() && !avengine::is_index_token_char(static_cast<unsigned char>(text[i])))
      {
        ++i;
      }
      if (i >= text.size())
      {
        break;
      }

      const size_t wordStart = i;
      size_t wordEnd = i;
      size_t digitCount = 0;

      while (wordEnd < text.size())
      {
        const unsigned char c = static_cast<unsigned char>(text[wordEnd]);
        if (avengine::is_index_token_char(c))
        {
          if (avengine::is_index_digit(c))
          {
            ++digitCount;
          }
          ++wordEnd;
          continue;
        }

        bool keepJoiner = false;
        if (avengine::is_index_apostrophe(c))
        {
          keepJoiner =
              (wordEnd > wordStart && wordEnd + 1 < text.size() &&
               avengine::is_index_token_char(static_cast<unsigned char>(text[wordEnd - 1])) &&
               avengine::is_index_token_char(static_cast<unsigned char>(text[wordEnd + 1])));
        }
        else if (c == '-')
        {
          const bool hasSides = (wordEnd > wordStart && wordEnd + 1 < text.size());
          if (hasSides && avengine::is_index_alpha(static_cast<unsigned char>(text[wordEnd + 1])) &&
              avengine::is_common_hyphen_prefix(text.begin() + wordStart, text.begin() + wordEnd))
          {
            keepJoiner = true;
          }
        }

        if (!keepJoiner)
        {
          break;
        }
        ++wordEnd;
      }

      avengine::string normalized;
      if (kind == PhraseKind::TITLE)
      {
        normalized.pushBack('#');
      }

      for (size_t j = wordStart; j < wordEnd; ++j)
      {
        const unsigned char c = static_cast<unsigned char>(text[j]);
        if (avengine::is_index_alpha(c))
        {
          normalized.pushBack(static_cast<char>(avengine::lower_index_ascii(c)));
        }
        else if (avengine::is_index_digit(c))
        {
          normalized.pushBack(static_cast<char>(c));
        }
      }

      if (avengine::index_token_digits_allowed(digitCount) &&
          normalized.size() > (kind == PhraseKind::TITLE ? 1u : 0u))
      {
        idx.ingest(avengine::string_slice(normalized));
        ++words;
      }

      i = wordEnd + (wordEnd < text.size() ? 1 : 0);
    }
  }

  return words;
}

void* serializeIndex(Index* index)
{
  // Atomically claim a unique filename counter so concurrent flushes never
  // collide on the same filename.
  size_t counter = indexCounter.fetch_add(1);

  char filename[16];
  snprintf(filename, sizeof(filename), "%05zu.sfi", counter);

  char tmpFilename[20];
  snprintf(tmpFilename, sizeof(tmpFilename), "%05zu.sfi.tmp", counter);

  int fd = open(tmpFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    // TODO: Log failure.
    delete index;
    return nullptr;
  }

  index->serialize(fd);
  close(fd);

  if (rename(tmpFilename, filename) < 0)
  {
    // TODO: Log failure.
    unlink(tmpFilename);
  }

  delete index;
  return nullptr;
}

void cleanUpIndexer()
{
  // Flush the live Index* in every slot (direct call, not a new thread, so we
  // can safely join all background threads afterwards).
  for (size_t i = 0; i < INDEX_SLOT_COUNT; i++)
  {
    pthread_mutex_lock(&globalIndexSlots[i].mutex);
    Index* live = globalIndexSlots[i].index;
    globalIndexSlots[i].index = nullptr;
    pthread_mutex_unlock(&globalIndexSlots[i].mutex);

    if (live)
      serializeIndex(live);
  }

  // Join every background serialize thread that was spawned during ingestion.
  pthread_mutex_lock(&g_serializeThreadsMutex);
  avengine::vector<pthread_t> threads = g_serializeThreads;
  g_serializeThreads.clear();
  pthread_mutex_unlock(&g_serializeThreadsMutex);

  for (pthread_t tid : threads)
    pthread_join(tid, nullptr);

  closeAnchorLog();
}

void resumeIndexer()
{
  // Find the largest .sfi file index and set indexCounter to the next value.
  DIR* dir = opendir(INDEX_DIRECTORY);
  if (dir == nullptr)
  {
    openAnchorLog();
    return;
  }

  size_t maxIndex = 0;
  bool found = false;

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr)
  {
    const char* name = entry->d_name;
    size_t len = strlen(name);

    // Filenames have the form "%05zu.sfi" (9 chars: 5 digits + '.' + 3 chars).
    if (len != 9)
      continue;

    if (strcmp(name + 5, ".sfi") != 0)
      continue;

    // Ensure the first 5 characters are all digits.
    bool allDigits = true;
    for (int i = 0; i < 5; i++)
    {
      if (name[i] < '0' || name[i] > '9')
      {
        allDigits = false;
        break;
      }
    }
    if (!allDigits)
    {
      continue;
    }

    char numBuf[6];
    memcpy(numBuf, name, 5);
    numBuf[5] = '\0';
    size_t idx = static_cast<size_t>(atoi(numBuf));

    if (!found || idx > maxIndex)
    {
      maxIndex = idx;
      found = true;
    }
  }

  closedir(dir);

  if (found)
    indexCounter.store(maxIndex + 1);

  openAnchorLog();
}

size_t indexDocument(DocumentMetadata& document_metadata,
                     avengine::vector<avengine::string_slice>& phrases)
{
  // Select this document's slot via a round-robin atomic counter.
  size_t slotIdx = g_nextSlot.fetch_add(1) % INDEX_SLOT_COUNT;
  IndexSlot& slot = globalIndexSlots[slotIdx];

  pthread_mutex_lock(&slot.mutex);
  slot.index->beginDocument(document_metadata);
  size_t total = 0;
  total += tokenizeInto(phrases, *slot.index, PhraseKind::BODY);
  avengine::string titleCopy = document_metadata.title;
  avengine::vector<avengine::string_slice> titlePhrases;
  titlePhrases.pushBack(avengine::string_slice(titleCopy));
  total += tokenizeInto(titlePhrases, *slot.index, PhraseKind::TITLE);
  slot.index->endDocument();

  if (slot.index->offset >= OFFSET_THRESHOLD)
  {
    avengine::crawler::globalCrawlLogger().log("FLUSHING INDEX", slotIdx, slot.index->offset,
                                               slot.index->totalUniqWords,
                                               slot.index->currentDocId);
    Index* oldIndex = slot.index;
    slot.index = new Index();

    pthread_t tid;
    pthread_create(&tid, nullptr, reinterpret_cast<void* (*)(void*)>(serializeIndex), oldIndex);

    pthread_mutex_lock(&g_serializeThreadsMutex);
    g_serializeThreads.pushBack(tid);
    pthread_mutex_unlock(&g_serializeThreadsMutex);
  }

  pthread_mutex_unlock(&slot.mutex);
  return total;
}
