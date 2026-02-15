#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../src/index.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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

TEST_CASE("Encoding helpers")
{
  uint8_t varBuf[8] = {};

  uint8_t* varEnd = write_var_uint32(varBuf, 0u);
  CHECK((varEnd - varBuf) == 1);
  CHECK(varBuf[0] == 0x00);

  varEnd = write_var_uint32(varBuf, 127u);
  CHECK((varEnd - varBuf) == 1);
  CHECK(varBuf[0] == 0x7F);

  varEnd = write_var_uint32(varBuf, 128u);
  CHECK((varEnd - varBuf) == 2);
  CHECK(varBuf[0] == 0x80);
  CHECK(varBuf[1] == 0x01);

  varEnd = write_var_uint32(varBuf, 300u);
  CHECK((varEnd - varBuf) == 2);
  CHECK(varBuf[0] == 0xAC);
  CHECK(varBuf[1] == 0x02);

  uint8_t u64Buf[8] = {};
  const uint64_t expected = 0x0123456789ABCDEFULL;
  uint8_t* u64End = write_uint64(u64Buf, expected);
  CHECK((u64End - u64Buf) == 8);
  CHECK(readU64LE(u64Buf) == expected);
}

TEST_CASE("PostingList ingest tracks fields")
{
  PostingList list("alpha", nullptr);

  list.ingest(2u, 10u, PhraseKind::NORMAL);
  list.ingest(2u, 42u, PhraseKind::TITLE);
  list.ingest(9u, 100u, PhraseKind::ANCHOR);

  CHECK(std::strcmp(list.word, "alpha") == 0);
  CHECK(list.nextBucket == nullptr);

  REQUIRE(list.postings.size() == 2);
  CHECK(list.postings[0].docId == 2u);
  CHECK(list.postings[0].normalTermFreq == 1u);
  CHECK(list.postings[0].titleTermFreq == 1u);
  CHECK(list.postings[0].anchorTermFreq == 0u);

  CHECK(list.postings[1].docId == 9u);
  CHECK(list.postings[1].normalTermFreq == 0u);
  CHECK(list.postings[1].titleTermFreq == 0u);
  CHECK(list.postings[1].anchorTermFreq == 1u);

  CHECK(list.normalPositions.size() == 1);
  CHECK(list.normalPositions[0] == 10u);
  CHECK(list.titlePositions.size() == 1);
  CHECK(list.titlePositions[0] == 42u);
  CHECK(list.anchorPositions.size() == 1);
  CHECK(list.anchorPositions[0] == 100u);
}

TEST_CASE("Index ingest across documents")
{
  Index index;

  CHECK(index.beginDocument("doc://0") == 0u);
  index.ingest("apple");
  index.ingest("banana");
  index.ingest("apple");

  CHECK(index.beginDocument("doc://1") == 1u);
  index.ingest("apple");

  CHECK(index.documentNames.size() == 2);
  CHECK(index.currentDocId == 1u);
  CHECK(index.totalUniqWords == 2u);
  CHECK(index.normalOffset == 4u);

  PostingList* apple = index.getBucket("apple");
  PostingList* banana = index.getBucket("banana");

  Posting* appleDoc0 = findPostingByDoc(apple, 0u);
  Posting* appleDoc1 = findPostingByDoc(apple, 1u);
  REQUIRE(appleDoc0 != nullptr);
  REQUIRE(appleDoc1 != nullptr);
  CHECK(appleDoc0->normalTermFreq == 2u);
  CHECK(appleDoc1->normalTermFreq == 1u);
  CHECK(appleDoc0->titleTermFreq == 0u);
  CHECK(appleDoc1->anchorTermFreq == 0u);

  CHECK(apple->normalPositions.size() == 3);
  CHECK(apple->normalPositions[0] == 0u);
  CHECK(apple->normalPositions[1] == 2u);
  CHECK(apple->normalPositions[2] == 3u);

  Posting* bananaDoc0 = findPostingByDoc(banana, 0u);
  REQUIRE(bananaDoc0 != nullptr);
  CHECK(bananaDoc0->normalTermFreq == 1u);
  CHECK(banana->normalPositions.size() == 1);
  CHECK(banana->normalPositions[0] == 1u);
}

TEST_CASE("Anchor target assignment and field isolation")
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

  REQUIRE(postingA != nullptr);
  REQUIRE(postingB != nullptr);

  CHECK(postingA->normalTermFreq == 1u);
  CHECK(postingA->titleTermFreq == 1u);
  CHECK(postingA->anchorTermFreq == 0u);

  CHECK(postingB->normalTermFreq == 0u);
  CHECK(postingB->titleTermFreq == 0u);
  CHECK(postingB->anchorTermFreq == 2u);

  CHECK(search->normalPositions.size() == 1u);
  CHECK(search->titlePositions.size() == 1u);
  CHECK(search->anchorPositions.size() == 2u);

  CHECK(index.normalOffset == 1u);
  CHECK(index.titleOffset == 1u);
  CHECK(index.anchorOffset == 2u);
}

TEST_CASE("indexDocument tokenization")
{
  char text[] = "Alpha, beta! ALPHA?? 123 gamma.";
  const size_t words = indexDocument("doc://tokenized", text);

  CHECK(words == 4u);
  CHECK(globalIndex.documentNames.size() >= 1);

  PostingList* alpha = globalIndex.getBucket("alpha");
  PostingList* beta = globalIndex.getBucket("beta");
  PostingList* gamma = globalIndex.getBucket("gamma");

  Posting* alphaPosting = findPostingByDoc(alpha, 0u);
  Posting* betaPosting = findPostingByDoc(beta, 0u);
  Posting* gammaPosting = findPostingByDoc(gamma, 0u);

  REQUIRE(alphaPosting != nullptr);
  REQUIRE(betaPosting != nullptr);
  REQUIRE(gammaPosting != nullptr);

  CHECK(alphaPosting->normalTermFreq == 2u);
  CHECK(betaPosting->normalTermFreq == 1u);
  CHECK(gammaPosting->normalTermFreq == 1u);

  CHECK(alpha->normalPositions.size() >= 2);
  CHECK(alpha->normalPositions[0] == 0u);
  CHECK(alpha->normalPositions[1] == 2u);
  CHECK(beta->normalPositions.size() >= 1);
  CHECK(beta->normalPositions[0] == 1u);
  CHECK(gamma->normalPositions.size() >= 1);
  CHECK(gamma->normalPositions[0] == 3u);
}

TEST_CASE("v2 serialization header and field sections")
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
  REQUIRE(fd >= 0);

  index.serialize(fd);
  CHECK(close(fd) == 0);

  struct stat st;
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

  CHECK(readU64LE(bytes) == INDEX_FILE_MAGIC_V2);
  CHECK(readU64LE(bytes + 8) == INDEX_FILE_VERSION_V2);
  CHECK(readU64LE(bytes + 16) == 1u);
  const uint64_t uniqueWordsPlusOne = readU64LE(bytes + 24);
  CHECK(uniqueWordsPlusOne == 2u);
  CHECK(readU64LE(bytes + 32) == INDEX_FILE_FIELD_COUNT_V2);

  const size_t dictionaryBytes = static_cast<size_t>(40u + (uniqueWordsPlusOne * 2u * 16u));
  const size_t stringBytes = std::strlen("alpha") + 1u;
  const size_t postingsStart = dictionaryBytes + stringBytes;

  REQUIRE(fileSize >= postingsStart + 56u);

  const uint8_t* posting = bytes + postingsStart;
  CHECK(readU64LE(posting) == 1u);
  CHECK(readU64LE(posting + 8) == 10u);
  CHECK(readU64LE(posting + 16) == 2u);
  CHECK(readU64LE(posting + 24) == 3u);
  CHECK(readU64LE(posting + 32) == 3u);
  CHECK(readU64LE(posting + 40) == 0u);
  CHECK(readU64LE(posting + 48) == 0u);

  const uint8_t* cursor = posting + 56u;
  const uint8_t* end = bytes + fileSize;

  CHECK(readVarU32(cursor, end) == 3u);
  CHECK(readVarU32(cursor, end) == 3u);
  CHECK(readVarU32(cursor, end) == 3u);

  CHECK(readVarU32(cursor, end) == 6u);
  CHECK(readVarU32(cursor, end) == 6u);
  CHECK(readVarU32(cursor, end) == 6u);

  CHECK(readVarU32(cursor, end) == 9u);
  CHECK(readVarU32(cursor, end) == 9u);
  CHECK(readVarU32(cursor, end) == 9u);

  CHECK(readVarU32(cursor, end) == 0u);
  for (int i = 0; i < 9; ++i)
    CHECK(readVarU32(cursor, end) == 1u);

  CHECK(readVarU32(cursor, end) == 0u);
  CHECK(readVarU32(cursor, end) == 1u);

  CHECK(readVarU32(cursor, end) == 0u);
  CHECK(readVarU32(cursor, end) == 1u);
  CHECK(readVarU32(cursor, end) == 1u);

  CHECK(cursor == end);
  std::free(bytes);
}

} // namespace
