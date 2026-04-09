#include <avengine/codec.h>
#include <avengine/indexer/indexer.h>
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

void testEncodingHelpers()
{
  {
    avengine::Writer w;
    w.writeVarU32(0u);
    EXPECT(w.size() == 1);
    EXPECT(w.data()[0] == 0x00);
  }
  {
    avengine::Writer w;
    w.writeVarU32(127u);
    EXPECT(w.size() == 1);
    EXPECT(w.data()[0] == 0x7F);
  }
  {
    avengine::Writer w;
    w.writeVarU32(128u);
    EXPECT(w.size() == 2);
    EXPECT(w.data()[0] == 0x80);
    EXPECT(w.data()[1] == 0x01);
  }
  {
    avengine::Writer w;
    w.writeVarU32(300u);
    EXPECT(w.size() == 2);
    EXPECT(w.data()[0] == 0xAC);
    EXPECT(w.data()[1] == 0x02);
  }
  {
    const uint64_t expected = 0x0123456789ABCDEFULL;
    avengine::Writer w;
    w.writeU64(expected);
    EXPECT(w.size() == 8);
    avengine::Reader r(w.data(), w.size());
    auto v = r.readU64();
    EXPECT(v == expected);
  }
}

void testPostingListIngest()
{
  PostingList list("alpha");

  list.ingest(2u, 10u);
  list.ingest(2u, 42u);
  list.ingest(9u, 100u);

  EXPECT(list.word == avengine::string("alpha"));

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

  uint32_t doc0 =
      index.beginDocument({.url = "doc://0", .title = "Doc 0", .description = "", .input = 0});
  EXPECT(doc0 == 0u);
  index.ingest("apple");
  index.ingest("banana");
  index.ingest("apple");
  index.endDocument();

  uint32_t doc1 =
      index.beginDocument({.url = "doc://1", .title = "Doc 1", .description = "", .input = 0});
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
  char text[] =
      "Alpha, beta! ALPHA?? 1234 abc1234 12ab34 x9y8z7 abc12345 12345 12a34b56 gamma.";
  DocumentMetadata meta{
      .url = "doc://tokenized", .title = "Title9 Case12345", .description = "", .input = 0};
  avengine::vector<avengine::string_slice> phrases;
  phrases.pushBack(avengine::string_slice(text, sizeof(text) - 1));
  const size_t words = indexDocument(meta, phrases);

  EXPECT(words == 9u);
  // The round-robin counter starts at 0, so the first indexDocument() call
  // always lands in slot 0 (assuming no prior indexDocument() calls in the
  // test binary, which is the case here since this is the first such call).
  Index* activeIndex = globalIndexSlots[0].index;
  EXPECT(activeIndex->documentMetadata.size() == 1);

  PostingList* alpha = activeIndex->getBucket("alpha");
  PostingList* beta = activeIndex->getBucket("beta");
  PostingList* fourDigits = activeIndex->getBucket("1234");
  PostingList* mixedA = activeIndex->getBucket("abc1234");
  PostingList* mixedB = activeIndex->getBucket("12ab34");
  PostingList* mixedC = activeIndex->getBucket("x9y8z7");
  PostingList* gamma = activeIndex->getBucket("gamma");
  PostingList* title = activeIndex->getBucket("#title9");

  EXPECT(alpha->postings.size() == 1);
  EXPECT(alpha->postings[0].termFreq == 2u);
  EXPECT(beta->postings.size() == 1);
  EXPECT(beta->postings[0].termFreq == 1u);
  EXPECT(fourDigits->postings.size() == 1);
  EXPECT(fourDigits->postings[0].termFreq == 1u);
  EXPECT(mixedA->postings.size() == 1);
  EXPECT(mixedA->postings[0].termFreq == 1u);
  EXPECT(mixedB->postings.size() == 1);
  EXPECT(mixedB->postings[0].termFreq == 1u);
  EXPECT(mixedC->postings.size() == 1);
  EXPECT(mixedC->postings[0].termFreq == 1u);
  EXPECT(gamma->postings.size() == 1);
  EXPECT(gamma->postings[0].termFreq == 1u);
  EXPECT(title->postings.size() == 1);
  EXPECT(title->postings[0].termFreq == 1u);

  EXPECT(alpha->positions.size() == 2);
  EXPECT(alpha->positions[0] == 0u);
  EXPECT(alpha->positions[1] == 2u);
  EXPECT(beta->positions.size() == 1);
  EXPECT(beta->positions[0] == 1u);
  EXPECT(fourDigits->positions.size() == 1);
  EXPECT(fourDigits->positions[0] == 3u);
  EXPECT(mixedA->positions.size() == 1);
  EXPECT(mixedA->positions[0] == 4u);
  EXPECT(mixedB->positions.size() == 1);
  EXPECT(mixedB->positions[0] == 5u);
  EXPECT(mixedC->positions.size() == 1);
  EXPECT(mixedC->positions[0] == 6u);
  EXPECT(gamma->positions.size() == 1);
  EXPECT(gamma->positions[0] == 7u);
  EXPECT(title->positions.size() == 1);
  EXPECT(title->positions[0] == 8u);

  EXPECT(activeIndex->map.get("abc12345") == nullptr);
  EXPECT(activeIndex->map.get("12345") == nullptr);
  EXPECT(activeIndex->map.get("12a34b56") == nullptr);
  EXPECT(activeIndex->map.get("#case12345") == nullptr);
}

void testSerializationHeaderAndSkipPointers()
{
  Index index;
  index.beginDocument({.url = "doc://serialize",
                       .title = "Serialize",
                       .description = "Serialized description",
                       .input = 7});
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

  avengine::Reader fileHeader(bytes, fileSize);
  auto magicVal = fileHeader.readU64();
  auto docCountVal = fileHeader.readU64();
  auto uniqueWordsVal = fileHeader.readU64();
  auto tokenOffsetVal = fileHeader.readU64();
  auto rankingInputSizeVal = fileHeader.readU64();

  EXPECT(magicVal == 0x008FB8EF84979FF0ULL);
  EXPECT(docCountVal == 1u);
  EXPECT(uniqueWordsVal == 2u); // alpha + DOCUMENT_END
  EXPECT(tokenOffsetVal == 11u);
  EXPECT(rankingInputSizeVal == sizeof(RankingInput));

  uint64_t alphaPostingOffset = UINT64_MAX;
  uint64_t docEndPostingOffset = UINT64_MAX;
  for (uint64_t i = 0; i < uniqueWordsVal; ++i)
  {
    avengine::string word = fileHeader.readString();
    auto positionsCount = fileHeader.readU64();
    auto postingsCount = fileHeader.readU64();
    auto postingOffset = fileHeader.readU64();

    if (word == avengine::string("alpha"))
    {
      EXPECT(positionsCount == 10u);
      EXPECT(postingsCount == 1u);
      alphaPostingOffset = postingOffset;
    }
    if (word == avengine::string(DOCEND_TOKEN))
    {
      EXPECT(positionsCount == 1u);
      EXPECT(postingsCount == 1u);
      docEndPostingOffset = postingOffset;
    }
  }

  EXPECT(alphaPostingOffset != UINT64_MAX);
  EXPECT(docEndPostingOffset != UINT64_MAX);

  const size_t postingsStart = static_cast<size_t>(fileHeader.cursor() - bytes);
  const size_t alphaStart = postingsStart + static_cast<size_t>(alphaPostingOffset);
  EXPECT(alphaStart + 24u <= fileSize);

  avengine::Reader postReader(bytes + alphaStart, fileSize - alphaStart);
  {
    auto v = postReader.readU64();
    EXPECT(v == 1u);
  }
  {
    auto v = postReader.readU64();
    EXPECT(v == 10u);
  }
  {
    auto v = postReader.readU64();
    EXPECT(v == 3u);
  }

  // Helper to read one varint and check its value.
  auto checkVar = [&](uint32_t expected_val)
  {
    auto v = postReader.readVarU32();
    EXPECT(v == expected_val);
  };

  checkVar(3u);
  checkVar(3u);
  checkVar(3u); // skip pointer 0
  checkVar(6u);
  checkVar(6u);
  checkVar(6u); // skip pointer 1
  checkVar(9u);
  checkVar(9u);
  checkVar(9u); // skip pointer 2

  checkVar(0u); // first position delta
  for (int i = 0; i < 9; ++i)
    checkVar(1u); // remaining position deltas

  const size_t docEndStart = postingsStart + static_cast<size_t>(docEndPostingOffset);
  EXPECT(docEndStart + 24u <= fileSize);

  avengine::Reader docEndReader(bytes + docEndStart, fileSize - docEndStart);
  EXPECT(docEndReader.readU64() == 1u);
  EXPECT(docEndReader.readU64() == 1u);
  EXPECT(docEndReader.readU64() == 0u);
  EXPECT(docEndReader.readVarU32() == 10u);
  EXPECT(docEndReader.readString() == avengine::string("doc://serialize"));
  EXPECT(docEndReader.readString() == avengine::string("Serialize"));
  EXPECT(docEndReader.readString() == avengine::string("Serialized description"));
  EXPECT(docEndReader.readU64() == 7u);

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
  a->docId = a->slot->index->beginDocument(
      {.url = "doc://thread", .title = "Thread Doc", .description = "", .input = 0});
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
