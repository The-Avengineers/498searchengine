#include "../src/index.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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

  fail("valid varint stream", __LINE__);
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

void testEncodingHelpers()
{
  uint8_t varBuf[8] = {};

  uint8_t* varEnd = write_var_uint32(varBuf, 0u);
  EXPECT((varEnd - varBuf) == 1);
  EXPECT(varBuf[0] == 0x00);

  varEnd = write_var_uint32(varBuf, 127u);
  EXPECT((varEnd - varBuf) == 1);
  EXPECT(varBuf[0] == 0x7F);

  varEnd = write_var_uint32(varBuf, 128u);
  EXPECT((varEnd - varBuf) == 2);
  EXPECT(varBuf[0] == 0x80);
  EXPECT(varBuf[1] == 0x01);

  varEnd = write_var_uint32(varBuf, 300u);
  EXPECT((varEnd - varBuf) == 2);
  EXPECT(varBuf[0] == 0xAC);
  EXPECT(varBuf[1] == 0x02);

  uint8_t u64Buf[8] = {};
  const uint64_t expected = 0x0123456789ABCDEFULL;
  uint8_t* u64End = write_uint64(u64Buf, expected);
  EXPECT((u64End - u64Buf) == 8);
  EXPECT(readU64LE(u64Buf) == expected);
}

void testPostingListIngest()
{
  PostingList list("alpha", nullptr);

  list.ingest(2u, 10u, PhraseKind::NORMAL);
  list.ingest(2u, 42u, PhraseKind::TITLE);
  list.ingest(9u, 100u, PhraseKind::ANCHOR);

  EXPECT(std::strcmp(list.word, "alpha") == 0);
  EXPECT(list.nextBucket == nullptr);

  EXPECT(list.postings.size() == 2);
  EXPECT(list.postings[0].docId == 2u);
  EXPECT(list.postings[0].normalTermFreq == 1u);
  EXPECT(list.postings[0].titleTermFreq == 1u);
  EXPECT(list.postings[0].anchorTermFreq == 0u);

  EXPECT(list.postings[1].docId == 9u);
  EXPECT(list.postings[1].normalTermFreq == 0u);
  EXPECT(list.postings[1].titleTermFreq == 0u);
  EXPECT(list.postings[1].anchorTermFreq == 1u);

  EXPECT(list.normalPositions.size() == 1);
  EXPECT(list.normalPositions[0] == 10u);
  EXPECT(list.titlePositions.size() == 1);
  EXPECT(list.titlePositions[0] == 42u);
  EXPECT(list.anchorPositions.size() == 1);
  EXPECT(list.anchorPositions[0] == 100u);
}

void testIndexIngestAcrossDocuments()
{
  Index index;

  EXPECT(index.beginDocument("doc://0") == 0u);
  index.ingest("apple");
  index.ingest("banana");
  index.ingest("apple");

  EXPECT(index.beginDocument("doc://1") == 1u);
  index.ingest("apple");

  EXPECT(index.documentNames.size() == 2);
  EXPECT(index.currentDocId == 1u);
  EXPECT(index.totalUniqWords == 2u);
  EXPECT(index.normalOffset == 4u);

  PostingList* apple = index.getBucket("apple");
  PostingList* banana = index.getBucket("banana");

  Posting* appleDoc0 = findPostingByDoc(apple, 0u);
  Posting* appleDoc1 = findPostingByDoc(apple, 1u);
  EXPECT(appleDoc0 != nullptr);
  EXPECT(appleDoc1 != nullptr);
  EXPECT(appleDoc0->normalTermFreq == 2u);
  EXPECT(appleDoc1->normalTermFreq == 1u);
  EXPECT(appleDoc0->titleTermFreq == 0u);
  EXPECT(appleDoc1->anchorTermFreq == 0u);

  EXPECT(apple->normalPositions.size() == 3);
  EXPECT(apple->normalPositions[0] == 0u);
  EXPECT(apple->normalPositions[1] == 2u);
  EXPECT(apple->normalPositions[2] == 3u);

  Posting* bananaDoc0 = findPostingByDoc(banana, 0u);
  EXPECT(bananaDoc0 != nullptr);
  EXPECT(bananaDoc0->normalTermFreq == 1u);
  EXPECT(banana->normalPositions.size() == 1);
  EXPECT(banana->normalPositions[0] == 1u);
}

void testAnchorTargetAssignmentAndFieldIsolation()
{
  Index index;

  const uint32_t docA = index.beginDocument("http://a.test/");
  const uint32_t docB = index.beginDocument("http://b.test/");

  index.ingestIntoDocument(docA, "search", PhraseKind::NORMAL);
  index.ingestIntoDocument(docA, "search", PhraseKind::TITLE);
  index.ingestIntoDocument(docB, "search", PhraseKind::ANCHOR);
  index.ingestIntoDocument(docB, "search", PhraseKind::ANCHOR);

  PostingList* search = index.getBucket("search");
  Posting* postingA = findPostingByDoc(search, docA);
  Posting* postingB = findPostingByDoc(search, docB);

  EXPECT(postingA != nullptr);
  EXPECT(postingB != nullptr);

  EXPECT(postingA->normalTermFreq == 1u);
  EXPECT(postingA->titleTermFreq == 1u);
  EXPECT(postingA->anchorTermFreq == 0u);

  EXPECT(postingB->normalTermFreq == 0u);
  EXPECT(postingB->titleTermFreq == 0u);
  EXPECT(postingB->anchorTermFreq == 2u);

  EXPECT(search->normalPositions.size() == 1u);
  EXPECT(search->titlePositions.size() == 1u);
  EXPECT(search->anchorPositions.size() == 2u);

  EXPECT(index.normalOffset == 1u);
  EXPECT(index.titleOffset == 1u);
  EXPECT(index.anchorOffset == 2u);
}

void testIndexDocumentTokenization()
{
  char text[] = "Alpha, beta! ALPHA?? 123 gamma.";
  const size_t words = indexDocument("doc://tokenized", text);

  EXPECT(words == 4u);
  EXPECT(globalIndex.documentNames.size() == 1);

  PostingList* alpha = globalIndex.getBucket("alpha");
  PostingList* beta = globalIndex.getBucket("beta");
  PostingList* gamma = globalIndex.getBucket("gamma");

  Posting* alphaPosting = findPostingByDoc(alpha, 0u);
  Posting* betaPosting = findPostingByDoc(beta, 0u);
  Posting* gammaPosting = findPostingByDoc(gamma, 0u);

  EXPECT(alphaPosting != nullptr);
  EXPECT(betaPosting != nullptr);
  EXPECT(gammaPosting != nullptr);

  EXPECT(alphaPosting->normalTermFreq == 2u);
  EXPECT(betaPosting->normalTermFreq == 1u);
  EXPECT(gammaPosting->normalTermFreq == 1u);

  EXPECT(alpha->normalPositions.size() == 2);
  EXPECT(alpha->normalPositions[0] == 0u);
  EXPECT(alpha->normalPositions[1] == 2u);
  EXPECT(beta->normalPositions.size() == 1);
  EXPECT(beta->normalPositions[0] == 1u);
  EXPECT(gamma->normalPositions.size() == 1);
  EXPECT(gamma->normalPositions[0] == 3u);
}

void testSerializationHeaderAndFieldSectionsV2()
{
  Index index;
  index.beginDocument("doc://serialize");

  for (uint32_t i = 0; i < 10u; ++i)
    index.ingest("alpha", PhraseKind::NORMAL);
  for (uint32_t i = 0; i < 2u; ++i)
    index.ingest("alpha", PhraseKind::TITLE);
  for (uint32_t i = 0; i < 3u; ++i)
    index.ingest("alpha", PhraseKind::ANCHOR);

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

  EXPECT(readU64LE(bytes) == INDEX_FILE_MAGIC_V2);
  EXPECT(readU64LE(bytes + 8) == INDEX_FILE_VERSION_V2);
  EXPECT(readU64LE(bytes + 16) == 1u);
  const uint64_t uniqueWordsPlusOne = readU64LE(bytes + 24);
  EXPECT(uniqueWordsPlusOne == 2u);
  EXPECT(readU64LE(bytes + 32) == INDEX_FILE_FIELD_COUNT_V2);

  const size_t dictionaryBytes = static_cast<size_t>(40u + (uniqueWordsPlusOne * 2u * 16u));
  const size_t stringBytes = std::strlen("alpha") + 1u;
  const size_t postingsStart = dictionaryBytes + stringBytes;

  EXPECT(fileSize >= postingsStart + 56u);

  const uint8_t* posting = bytes + postingsStart;
  EXPECT(readU64LE(posting) == 1u);
  EXPECT(readU64LE(posting + 8) == 10u);
  EXPECT(readU64LE(posting + 16) == 2u);
  EXPECT(readU64LE(posting + 24) == 3u);
  EXPECT(readU64LE(posting + 32) == 3u);
  EXPECT(readU64LE(posting + 40) == 0u);
  EXPECT(readU64LE(posting + 48) == 0u);

  const uint8_t* cursor = posting + 56u;
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
    EXPECT(readVarU32(cursor, end) == 1u);

  EXPECT(readVarU32(cursor, end) == 0u);
  EXPECT(readVarU32(cursor, end) == 1u);

  EXPECT(readVarU32(cursor, end) == 0u);
  EXPECT(readVarU32(cursor, end) == 1u);
  EXPECT(readVarU32(cursor, end) == 1u);

  EXPECT(cursor == end);
  std::free(bytes);
}

} // namespace

int main()
{
  testEncodingHelpers();
  testPostingListIngest();
  testIndexIngestAcrossDocuments();
  testAnchorTargetAssignmentAndFieldIsolation();
  testIndexDocumentTokenization();
  testSerializationHeaderAndFieldSectionsV2();
  return 0;
}
