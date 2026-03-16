#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../src/index.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
uint64_t readU64LE(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(ptr[i]) << (i * 8);
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

  REQUIRE_MESSAGE(false, "valid varint stream");
  return 0;
}

Posting* findPostingByDoc(PostingList* list, uint32_t docId)
{
  for (size_t i = 0; i < list->postings.size(); i++)
  {
    if (list->postings[i].docId == docId)
      return &list->postings[i];
  }
  return nullptr;
}

struct ConcurrentWorkerArgs
{
  Index* index;
  pthread_mutex_t* startMutex;
  pthread_cond_t* startCond;
  int* readyCount;
  bool* start;
  uint32_t* docId;
  const char* url;
  const char* term;
  PhraseKind kind;
};

void* ingestDocumentConcurrently(void* rawArgs)
{
  ConcurrentWorkerArgs* args = static_cast<ConcurrentWorkerArgs*>(rawArgs);
  *args->docId = args->index->beginDocument(args->url);

  pthread_mutex_lock(args->startMutex);
  *args->readyCount += 1;
  pthread_cond_signal(args->startCond);
  while (!*args->start)
    pthread_cond_wait(args->startCond, args->startMutex);
  pthread_mutex_unlock(args->startMutex);

  for (uint32_t i = 0; i < 1000u; ++i)
    args->index->ingestIntoDocument(*args->docId, args->term, args->kind);

  return nullptr;
}
} // namespace

TEST_CASE("Encoding helpers")
{
  avengine::vector<uint8_t> buf;

  append_var_uint32(buf, 0u);
  CHECK(buf.size() == 1);
  CHECK(buf[0] == 0x00);

  buf.clear();
  append_var_uint32(buf, 127u);
  CHECK(buf.size() == 1);
  CHECK(buf[0] == 0x7F);

  buf.clear();
  append_var_uint32(buf, 128u);
  CHECK(buf.size() == 2);
  CHECK(buf[0] == 0x80);
  CHECK(buf[1] == 0x01);

  buf.clear();
  append_var_uint32(buf, 300u);
  CHECK(buf.size() == 2);
  CHECK(buf[0] == 0xAC);
  CHECK(buf[1] == 0x02);

  buf.clear();
  const uint64_t expected = 0x0123456789ABCDEFULL;
  append_uint64(buf, expected);
  CHECK(buf.size() == 8);
  CHECK(readU64LE(buf.begin()) == expected);
}

TEST_CASE("PostingList ingest tracks one position stream")
{
  PostingList list("alpha", nullptr);

  list.ingest(2u, 10u);
  list.ingest(2u, 42u);
  list.ingest(9u, 100u);

  CHECK(std::strcmp(list.word, "alpha") == 0);
  CHECK(list.nextBucket == nullptr);

  REQUIRE(list.postings.size() == 2);
  CHECK(list.postings[0].docId == 2u);
  CHECK(list.postings[0].termFreq == 2u);
  CHECK(list.postings[1].docId == 9u);
  CHECK(list.postings[1].termFreq == 1u);

  REQUIRE(list.positions.size() == 3u);
  CHECK(list.positions[0] == 10u);
  CHECK(list.positions[1] == 42u);
  CHECK(list.positions[2] == 100u);
}

TEST_CASE("Index ingest across documents")
{
  Index index;

  const uint32_t doc0 = index.beginDocument("doc://0");
  CHECK(doc0 == 0u);
  index.ingestIntoDocument(doc0, "apple", PhraseKind::BODY);
  index.ingestIntoDocument(doc0, "banana", PhraseKind::BODY);
  index.ingestIntoDocument(doc0, "apple", PhraseKind::BODY);

  const uint32_t doc1 = index.beginDocument("doc://1");
  CHECK(doc1 == 1u);
  index.ingestIntoDocument(doc1, "apple", PhraseKind::BODY);

  CHECK(index.documentNames.size() == 2u);
  CHECK(index.totalUniqWords == 2u);
  CHECK(index.offset == 4u);

  PostingList* apple = index.getBucket("apple");
  PostingList* banana = index.getBucket("banana");

  Posting* appleDoc0 = findPostingByDoc(apple, 0u);
  Posting* appleDoc1 = findPostingByDoc(apple, 1u);
  REQUIRE(appleDoc0 != nullptr);
  REQUIRE(appleDoc1 != nullptr);
  CHECK(appleDoc0->termFreq == 2u);
  CHECK(appleDoc1->termFreq == 1u);

  REQUIRE(apple->positions.size() == 3u);
  CHECK(apple->positions[0] == 0u);
  CHECK(apple->positions[1] == 2u);
  CHECK(apple->positions[2] == 3u);

  Posting* bananaDoc0 = findPostingByDoc(banana, 0u);
  REQUIRE(bananaDoc0 != nullptr);
  CHECK(bananaDoc0->termFreq == 1u);
  REQUIRE(banana->positions.size() == 1u);
  CHECK(banana->positions[0] == 1u);
}

TEST_CASE("Decorated terms isolate body title and url")
{
  Index index;

  const uint32_t docA = index.beginDocument("http://a.test/");
  const uint32_t docB = index.beginDocument("http://b.test/");

  index.ingestIntoDocument(docA, "search", PhraseKind::BODY);
  index.ingestIntoDocument(docA, "search", PhraseKind::TITLE);
  index.ingestIntoDocument(docB, "search", PhraseKind::URL);
  index.ingestIntoDocument(docB, "search", PhraseKind::URL);

  PostingList* body = index.getBucket("search");
  PostingList* title = index.getBucket("#search");
  PostingList* url = index.getBucket("/search");

  Posting* bodyPosting = findPostingByDoc(body, docA);
  Posting* titlePosting = findPostingByDoc(title, docA);
  Posting* urlPosting = findPostingByDoc(url, docB);

  REQUIRE(bodyPosting != nullptr);
  REQUIRE(titlePosting != nullptr);
  REQUIRE(urlPosting != nullptr);

  CHECK(bodyPosting->termFreq == 1u);
  CHECK(titlePosting->termFreq == 1u);
  CHECK(urlPosting->termFreq == 2u);

  CHECK(findPostingByDoc(body, docB) == nullptr);
  CHECK(findPostingByDoc(title, docB) == nullptr);
  CHECK(findPostingByDoc(url, docA) == nullptr);

  CHECK(body->positions.size() == 1u);
  CHECK(title->positions.size() == 1u);
  CHECK(url->positions.size() == 2u);
  CHECK(index.offset == 4u);
}

TEST_CASE("Concurrent ingestion keeps postings attached to the correct document")
{
  Index index;
  pthread_mutex_t startMutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t startCond = PTHREAD_COND_INITIALIZER;
  int readyCount = 0;
  bool start = false;
  uint32_t docA = UINT32_MAX;
  uint32_t docB = UINT32_MAX;

  ConcurrentWorkerArgs argsA = {&index, &startMutex, &startCond, &readyCount,
                                &start, &docA, "doc://alpha", "alpha", PhraseKind::BODY};
  ConcurrentWorkerArgs argsB = {&index, &startMutex, &startCond, &readyCount,
                                &start, &docB, "doc://beta", "beta", PhraseKind::TITLE};
  pthread_t threadA = {};
  pthread_t threadB = {};

  REQUIRE(pthread_create(&threadA, nullptr, ingestDocumentConcurrently, &argsA) == 0);
  REQUIRE(pthread_create(&threadB, nullptr, ingestDocumentConcurrently, &argsB) == 0);

  REQUIRE(pthread_mutex_lock(&startMutex) == 0);
  while (readyCount != 2)
    REQUIRE(pthread_cond_wait(&startCond, &startMutex) == 0);
  start = true;
  REQUIRE(pthread_cond_broadcast(&startCond) == 0);
  REQUIRE(pthread_mutex_unlock(&startMutex) == 0);

  REQUIRE(pthread_join(threadA, nullptr) == 0);
  REQUIRE(pthread_join(threadB, nullptr) == 0);
  REQUIRE(pthread_cond_destroy(&startCond) == 0);
  REQUIRE(pthread_mutex_destroy(&startMutex) == 0);

  REQUIRE(docA != UINT32_MAX);
  REQUIRE(docB != UINT32_MAX);
  CHECK(docA != docB);

  PostingList* alpha = index.getBucket("alpha");
  PostingList* beta = index.getBucket("#beta");

  Posting* alphaDocA = findPostingByDoc(alpha, docA);
  Posting* alphaDocB = findPostingByDoc(alpha, docB);
  Posting* betaDocA = findPostingByDoc(beta, docA);
  Posting* betaDocB = findPostingByDoc(beta, docB);

  REQUIRE(alphaDocA != nullptr);
  REQUIRE(betaDocB != nullptr);
  CHECK(alphaDocA->termFreq == 1000u);
  CHECK(betaDocB->termFreq == 1000u);
  CHECK(alphaDocB == nullptr);
  CHECK(betaDocA == nullptr);
  CHECK(index.offset == 2000u);
}

TEST_CASE("indexDocument tokenization indexes body terms")
{
  const uint32_t docId = static_cast<uint32_t>(globalIndex.documentNames.size());
  char text[] = "Alpha, beta! ALPHA?? 123 gamma.";
  const size_t words = indexDocument("doc://tokenized", text);

  CHECK(words == 4u);
  CHECK(globalIndex.documentNames.size() >= static_cast<size_t>(docId + 1u));

  PostingList* alpha = globalIndex.getBucket("alpha");
  PostingList* beta = globalIndex.getBucket("beta");
  PostingList* gamma = globalIndex.getBucket("gamma");

  Posting* alphaPosting = findPostingByDoc(alpha, docId);
  Posting* betaPosting = findPostingByDoc(beta, docId);
  Posting* gammaPosting = findPostingByDoc(gamma, docId);

  REQUIRE(alphaPosting != nullptr);
  REQUIRE(betaPosting != nullptr);
  REQUIRE(gammaPosting != nullptr);

  CHECK(alphaPosting->termFreq == 2u);
  CHECK(betaPosting->termFreq == 1u);
  CHECK(gammaPosting->termFreq == 1u);
}

TEST_CASE("v4 serialization stays compact")
{
  Index index;
  const uint32_t docId = index.beginDocument("doc://serialize");

  for (uint32_t i = 0; i < 10u; ++i)
    index.ingestIntoDocument(docId, "alpha", PhraseKind::BODY);

  char pathTemplate[] = "/tmp/index_test_XXXXXX";
  const int fd = mkstemp(pathTemplate);
  REQUIRE(fd >= 0);

  CHECK(index.serialize(fd) == false);
  CHECK(close(fd) == 0);

  struct stat st = {};
  REQUIRE(stat(pathTemplate, &st) == 0);
  REQUIRE(st.st_size > 0);

  const int readFd = open(pathTemplate, O_RDONLY);
  REQUIRE(readFd >= 0);

  const size_t fileSize = static_cast<size_t>(st.st_size);
  uint8_t* bytes = static_cast<uint8_t*>(std::malloc(fileSize));
  REQUIRE(bytes != nullptr);

  size_t totalRead = 0;
  while (totalRead < fileSize)
  {
    const ssize_t n = read(readFd, bytes + totalRead, fileSize - totalRead);
    REQUIRE(n >= 0);
    if (n == 0)
      break;
    totalRead += static_cast<size_t>(n);
  }
  CHECK(totalRead == fileSize);

  CHECK(close(readFd) == 0);
  CHECK(unlink(pathTemplate) == 0);

  CHECK(readU64LE(bytes) == INDEX_FILE_MAGIC);
  CHECK(readU64LE(bytes + 8) == INDEX_FILE_VERSION);
  CHECK(readU64LE(bytes + 16) == 1u);
  const uint64_t uniqueWordsPlusOne = readU64LE(bytes + 24);
  CHECK(uniqueWordsPlusOne == 2u);

  const size_t dictionaryBytes = static_cast<size_t>(32u + (uniqueWordsPlusOne * 2u * 16u));
  const size_t stringBytes = std::strlen("alpha") + 1u;
  const size_t postingsStart = dictionaryBytes + stringBytes;

  REQUIRE(fileSize >= postingsStart + 16u);

  const uint8_t* posting = bytes + postingsStart;
  CHECK(readU64LE(posting) == 1u);
  CHECK(readU64LE(posting + 8) == 10u);

  const uint8_t* cursor = posting + 16u;
  const uint8_t* end = bytes + fileSize;

  CHECK(readVarU32(cursor, end) == 0u);
  for (int i = 0; i < 9; ++i)
    CHECK(readVarU32(cursor, end) == 1u);

  CHECK(cursor == end);
  std::free(bytes);
}
