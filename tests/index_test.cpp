#include "../src/index.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
void fail(const char* expr, int line)
{
  std::fprintf(stderr, "index_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

uint64_t readU64LE(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
  {
    value |= static_cast<uint64_t>(ptr[i]) << (i * 8);
  }
  return value;
}

uint32_t readVarU32(const uint8_t*& cursor, const uint8_t* end)
{
  uint32_t result = 0;
  uint32_t shift = 0;

  while (cursor < end && shift < 35)
  {
    const uint8_t byte = *cursor++;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0)
      return result;
    shift += 7;
  }

  fail("valid varint stream", __LINE__);
  return 0;
}

void testEncodingHelpers()
{
  avengine::vector<uint8_t> buf;

  append_var_uint32(buf, 0u);
  EXPECT(buf.size() == 1);
  EXPECT(buf[0] == 0x00);

  buf.clear();
  append_var_uint32(buf, 127u);
  EXPECT(buf.size() == 1);
  EXPECT(buf[0] == 0x7F);

  buf.clear();
  append_var_uint32(buf, 128u);
  EXPECT(buf.size() == 2);
  EXPECT(buf[0] == 0x80);
  EXPECT(buf[1] == 0x01);

  buf.clear();
  append_var_uint32(buf, 300u);
  EXPECT(buf.size() == 2);
  EXPECT(buf[0] == 0xAC);
  EXPECT(buf[1] == 0x02);

  buf.clear();
  const uint64_t expected = 0x0123456789ABCDEFULL;
  append_uint64(buf, expected);
  EXPECT(buf.size() == 8);
  EXPECT(readU64LE(buf.begin()) == expected);
}

void testPostingListIngest()
{
  PostingList list("alpha", nullptr);

  list.ingest(2u, 10u);
  list.ingest(2u, 42u);
  list.ingest(9u, 100u);

  EXPECT(list.word == avengine::string("alpha"));
  EXPECT(list.nextBucket == nullptr);

  EXPECT(list.postings.size() == 2);
  EXPECT(list.postings[0].docId == 2u);
  EXPECT(list.postings[0].termFreq == 2u);
  EXPECT(list.postings[1].docId == 9u);
  EXPECT(list.postings[1].termFreq == 1u);

  EXPECT(list.positions.size() == 3);
  EXPECT(list.positions[0] == 10u);
  EXPECT(list.positions[1] == 42u);
  EXPECT(list.positions[2] == 100u);
}

void testIndexIngestAcrossDocuments()
{
  Index index;

  uint32_t doc0 = index.beginDocument({.url = "doc://0", .title = "Doc 0", .input = 0});
  EXPECT(doc0 == 0u);
  index.ingest("apple");
  index.ingest("banana");
  index.ingest("apple");
  index.endDocument();

  uint32_t doc1 = index.beginDocument({.url = "doc://1", .title = "Doc 1", .input = 0});
  EXPECT(doc1 == 1u);
  index.ingest("apple");
  index.endDocument();

  EXPECT(index.documentMetadata.size() == 2);
  EXPECT(index.totalUniqWords == 3u); // apple, banana, DOCEND_TOKEN
  EXPECT(index.offset == 6u);         // 3 words + DOCEND for doc0, 1 word + DOCEND for doc1

  PostingList* apple = index.getBucket("apple");
  PostingList* banana = index.getBucket("banana");

  EXPECT(apple->postings.size() == 2);
  EXPECT(apple->postings[0].docId == 0u);
  EXPECT(apple->postings[0].termFreq == 2u);
  EXPECT(apple->postings[1].docId == 1u);
  EXPECT(apple->postings[1].termFreq == 1u);
  EXPECT(apple->positions.size() == 3);
  EXPECT(apple->positions[0] == 0u);
  EXPECT(apple->positions[1] == 2u);
  EXPECT(apple->positions[2] == 4u); // offset 3 is DOCEND for doc0

  EXPECT(banana->postings.size() == 1);
  EXPECT(banana->postings[0].docId == 0u);
  EXPECT(banana->postings[0].termFreq == 1u);
  EXPECT(banana->positions.size() == 1);
  EXPECT(banana->positions[0] == 1u);
}

void testIndexDocumentTokenization()
{
  char text[] = "Alpha, beta! ALPHA?? 123 gamma.";
  DocumentMetadata meta{.url = "doc://tokenized", .title = "Title Case", .input = 0};
  avengine::vector<avengine::string_slice> phrases;
  phrases.pushBack(avengine::string_slice(text, sizeof(text) - 1));
  const size_t words = indexDocument(meta, phrases);

  EXPECT(words == 6u);
  EXPECT(globalIndex.index->documentMetadata.size() == 1);

  PostingList* alpha = globalIndex.index->getBucket("alpha");
  PostingList* beta = globalIndex.index->getBucket("beta");
  PostingList* gamma = globalIndex.index->getBucket("gamma");
  PostingList* title = globalIndex.index->getBucket("#title");
  PostingList* caseWord = globalIndex.index->getBucket("#case");

  EXPECT(alpha->postings.size() == 1);
  EXPECT(alpha->postings[0].termFreq == 2u);
  EXPECT(beta->postings.size() == 1);
  EXPECT(beta->postings[0].termFreq == 1u);
  EXPECT(gamma->postings.size() == 1);
  EXPECT(gamma->postings[0].termFreq == 1u);
  EXPECT(title->postings.size() == 1);
  EXPECT(title->postings[0].termFreq == 1u);
  EXPECT(caseWord->postings.size() == 1);
  EXPECT(caseWord->postings[0].termFreq == 1u);

  EXPECT(alpha->positions.size() == 2);
  EXPECT(alpha->positions[0] == 0u);
  EXPECT(alpha->positions[1] == 2u);
  EXPECT(beta->positions.size() == 1);
  EXPECT(beta->positions[0] == 1u);
  EXPECT(gamma->positions.size() == 1);
  EXPECT(gamma->positions[0] == 3u);
  EXPECT(title->positions.size() == 1);
  EXPECT(title->positions[0] == 4u);
  EXPECT(caseWord->positions.size() == 1);
  EXPECT(caseWord->positions[0] == 5u);
}

void testSerializationHeaderAndSkipPointers()
{
  Index index;
  index.beginDocument({.url = "doc://serialize", .title = "Serialize", .input = 0});
  for (uint32_t i = 0; i < 10u; ++i)
  {
    (void)i;
    index.ingest("alpha");
  }
  index.endDocument();

  char pathTemplate[] = "/tmp/index_test_XXXXXX";
  const int fd = mkstemp(pathTemplate);
  EXPECT(fd >= 0);

  index.serialize(fd);
  EXPECT(close(fd) == 0);

  struct stat st;
  EXPECT(stat(pathTemplate, &st) == 0);
  EXPECT(st.st_size > 0);

  const int readFd = open(pathTemplate, O_RDONLY);
  EXPECT(readFd >= 0);

  const size_t fileSize = static_cast<size_t>(st.st_size);
  uint8_t* bytes = static_cast<uint8_t*>(std::malloc(fileSize));
  EXPECT(bytes != nullptr);

  size_t totalRead = 0;
  while (totalRead < fileSize)
  {
    const ssize_t n = read(readFd, bytes + totalRead, fileSize - totalRead);
    EXPECT(n >= 0);
    if (n == 0)
      break;
    totalRead += static_cast<size_t>(n);
  }
  EXPECT(totalRead == fileSize);

  EXPECT(close(readFd) == 0);
  EXPECT(unlink(pathTemplate) == 0);

  EXPECT(readU64LE(bytes) == 1u);
  const uint64_t uniqueWordsPlusOne = readU64LE(bytes + 8);
  EXPECT(uniqueWordsPlusOne == 3u); // alpha + DOCEND_TOKEN + 1

  const size_t dictionaryBytes = static_cast<size_t>(16u + (uniqueWordsPlusOne * 2u * 16u));
  // String table: DOCUMENT_END\0 (13 bytes) then alpha\0 (6 bytes) — in hashmap bucket order.
  const size_t stringBytes = std::strlen("DOCUMENT_END") + 1u + std::strlen("alpha") + 1u;
  const size_t postingsStart = dictionaryBytes + stringBytes;

  // DOCUMENT_END posting comes first (lower hash bucket). Compute its byte size:
  //   3 u64 header + varint(10) delta + metadata (varint+url + varint+title + u64 input).
  const size_t docEndPostingSize = 24u  // [doc_freq][coll_freq][skip_count]
                                   + 1u // varint(10): position delta
                                   + 1u + std::strlen("doc://serialize") // varint(len) + url
                                   + 1u + std::strlen("Serialize")       // varint(len) + title
                                   + sizeof(uint64_t);                   // RankingInput

  EXPECT(fileSize >= postingsStart + docEndPostingSize + 24u);

  const uint8_t* posting = bytes + postingsStart + docEndPostingSize;
  EXPECT(readU64LE(posting) == 1u);
  EXPECT(readU64LE(posting + 8) == 10u);
  EXPECT(readU64LE(posting + 16) == 3u);

  const uint8_t* cursor = posting + 24u;
  const uint8_t* end = bytes + fileSize;

  EXPECT(readVarU32(cursor, end) == 3u);
  EXPECT(readVarU32(cursor, end) == 3u);
  EXPECT(readVarU32(cursor, end) == 3u);

  EXPECT(readVarU32(cursor, end) == 6u);
  EXPECT(readVarU32(cursor, end) == 6u);
  EXPECT(readVarU32(cursor, end) == 6u);

  EXPECT(readVarU32(cursor, end) == 9u);
  EXPECT(readVarU32(cursor, end) == 9u);
  EXPECT(readVarU32(cursor, end) == 9u);

  EXPECT(readVarU32(cursor, end) == 0u);
  for (int i = 0; i < 9; ++i)
  {
    EXPECT(readVarU32(cursor, end) == 1u);
  }

  EXPECT(cursor == end);
  std::free(bytes);
}

struct ConcurrentThreadArgs
{
  IndexSlot* slot;
  int wordCount;
  uint32_t docId;
};

static void* concurrentIngestThread(void* arg)
{
  ConcurrentThreadArgs* a = static_cast<ConcurrentThreadArgs*>(arg);
  pthread_mutex_lock(&a->slot->mutex);
  a->docId =
      a->slot->index->beginDocument({.url = "doc://thread", .title = "Thread Doc", .input = 0});
  for (int i = 0; i < a->wordCount; ++i)
    a->slot->index->ingest("token");
  a->slot->index->endDocument();
  pthread_mutex_unlock(&a->slot->mutex);
  return nullptr;
}

// Spawns kThreads threads that each call beginDocument() and ingestIntoDocument()
// concurrently. Verifies no writes are lost and every thread received a unique docId.
void testConcurrentIngest()
{
  constexpr int kThreads = 8;
  constexpr int kWordsPerDoc = 100;

  IndexSlot slot = {new Index(), PTHREAD_MUTEX_INITIALIZER};
  ConcurrentThreadArgs args[kThreads];
  pthread_t threads[kThreads];

  for (int i = 0; i < kThreads; ++i)
  {
    args[i] = {&slot, kWordsPerDoc, UINT32_MAX};
    pthread_create(&threads[i], nullptr, concurrentIngestThread, &args[i]);
  }
  for (int i = 0; i < kThreads; ++i)
    pthread_join(threads[i], nullptr);

  // No beginDocument() calls were lost.
  EXPECT(slot.index->documentMetadata.size() == static_cast<size_t>(kThreads));

  // No ingest() offset increments were lost (kWordsPerDoc words + 1 DOCEND per thread).
  EXPECT(slot.index->offset == static_cast<size_t>(kThreads * (kWordsPerDoc + 1)));

  // Every assigned docId is unique and in [0, kThreads).
  bool seen[kThreads] = {};
  for (int i = 0; i < kThreads; ++i)
  {
    EXPECT(args[i].docId < static_cast<uint32_t>(kThreads));
    EXPECT(!seen[args[i].docId]);
    seen[args[i].docId] = true;
  }

  // All position records were written.
  PostingList* token = slot.index->getBucket("token");
  EXPECT(token != nullptr);
  EXPECT(token->positions.size() == static_cast<size_t>(kThreads * kWordsPerDoc));

  // All term-frequency increments were recorded.
  size_t totalFreq = 0;
  for (size_t i = 0; i < token->postings.size(); ++i)
    totalFreq += token->postings[i].termFreq;
  EXPECT(totalFreq == static_cast<size_t>(kThreads * kWordsPerDoc));

  delete slot.index;
  pthread_mutex_destroy(&slot.mutex);
}

} // namespace

int main()
{
  testEncodingHelpers();
  testPostingListIngest();
  testIndexIngestAcrossDocuments();
  testIndexDocumentTokenization();
  testSerializationHeaderAndSkipPointers();
  testConcurrentIngest();
  return 0;
}
