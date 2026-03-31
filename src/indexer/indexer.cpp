#include <avengine/avengine_lib/vector.h>
#include <avengine/codec.h>
#include <avengine/indexer/indexer.h>
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

void Index::buildStringTable(avengine::vector<uint64_t>& dict, avengine::vector<char>& stringBuffer)
{
  map.forEach(
      [&](const avengine::string& /*key*/, PostingList& pl)
      {
        avengine::string_slice wordSlice(pl.word);
        size_t hash = djb2Hash(wordSlice) % (totalUniqWords * 2);

        // Linear probe for an empty slot.
        // TODO: Benchmark alternative probing strategies when querying the index.
        while (dict[2 + hash * 2] > 0)
        {
          hash += 1;
          hash %= (totalUniqWords * 2);
        }

        dict[2 + hash * 2] = stringBuffer.size();

        for (size_t i = 0; i < pl.word.size(); i++)
          stringBuffer.pushBack(pl.word[i]);
        stringBuffer.pushBack('\0');

        pl.hash = hash;
      });
}

bool Index::serialize(int fd)
{
  // TODO: We currently use an offset of zero as a sentinel to indicate an unused slot, however
  // zero is a perfectly valid offset. We either need another sentinel or make zero impossible.
  const size_t needed = 2 + (totalUniqWords + 1) * 2 * 2;
  avengine::vector<uint64_t> dictionaryBuffer(needed);

  // TODO: Write magic bytes to identify file format and version.
  dictionaryBuffer[0] = documentMetadata.size();
  dictionaryBuffer[1] = totalUniqWords + 1;

  avengine::vector<char> stringBuffer;
  buildStringTable(dictionaryBuffer, stringBuffer);

  // TODO: maybe fallocate?
  if (lseek(fd, needed * sizeof(uint64_t), SEEK_SET) == (off_t)-1)
    return true;
  if (write(fd, stringBuffer.begin(), stringBuffer.size()) < 0)
    return true;

  size_t postingsOffset = 0;
  avengine::vector<uint8_t> postingBuffer;
  bool ioError = false;

  map.forEach(
      [&](const avengine::string& /*key*/, PostingList& pl)
      {
        if (ioError)
          return;

        dictionaryBuffer[2 + pl.hash * 2 + 1] = postingsOffset;

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
  if (write(fd, dictionaryBuffer.begin(), needed * sizeof(uint64_t)) < 0)
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

IndexSlot globalIndex = {new Index(), PTHREAD_MUTEX_INITIALIZER};

// Walks `texts`, lowercases each alpha run in-place via the mutable slice, and
// ingests each resulting token into `idx` with the given field decorator.
// BODY terms are ingested as-is; TITLE terms get a '#' prefix.
// Must be called between beginDocument() and endDocument().
// Returns the number of tokens found.
size_t tokenizeInto(avengine::vector<avengine::string_slice>& texts, Index& idx, PhraseKind kind)
{
  size_t words = 0;

  for (auto text : texts)
  {
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

      if (kind == PhraseKind::BODY)
      {
        idx.ingest(text.substr(wordStart, i - wordStart));
      }
      else
      {
        avengine::string decorated;
        decorated.pushBack('#');
        avengine::string_slice token = text.substr(wordStart, i - wordStart);
        for (size_t j = 0; j < token.size(); j++)
          decorated.pushBack(token[j]);
        idx.ingest(avengine::string_slice(decorated));
      }
      words++;

      while (i < text.size() && !isalpha(text[i]))
        i++;
    }
  }

  return words;
}

size_t indexCounter = 0;
avengine::vector<pthread_t> serializeThreads;

void* serializeIndex(Index* index)
{
  // Write to a temporary file first, then atomically rename it into place.
  // This ensures that a reader never sees a partially-written index file.
  size_t counter = indexCounter++;
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
  serializeIndex(globalIndex.index);

  for (size_t i = 0; i < serializeThreads.size(); i++)
  {
    pthread_join(serializeThreads[i], nullptr);
  }
}

void resumeIndexer()
{
  // Find the largest .sfi file index and set indexCounter to the next value.
  DIR* dir = opendir(INDEX_DIRECTORY);
  if (dir == nullptr)
  {
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
    indexCounter = maxIndex + 1;
}

size_t indexDocument(DocumentMetadata& document_metadata,
                     avengine::vector<avengine::string_slice>& phrases)
{
  pthread_mutex_lock(&globalIndex.mutex);
  globalIndex.index->beginDocument(document_metadata);
  size_t total = 0;
  total += tokenizeInto(phrases, *globalIndex.index, PhraseKind::BODY);
  avengine::string titleCopy = document_metadata.title;
  avengine::vector<avengine::string_slice> titlePhrases;
  titlePhrases.pushBack(avengine::string_slice(titleCopy));
  total += tokenizeInto(titlePhrases, *globalIndex.index, PhraseKind::TITLE);
  globalIndex.index->endDocument();

  if (globalIndex.index->offset >= OFFSET_THRESHOLD)
  {
    Index* oldIndex = globalIndex.index;
    globalIndex.index = new Index();

    pthread_t tid;
    pthread_create(&tid, nullptr, reinterpret_cast<void* (*)(void*)>(serializeIndex), oldIndex);

    serializeThreads.pushBack(tid);
  }

  pthread_mutex_unlock(&globalIndex.mutex);
  return total;
}
