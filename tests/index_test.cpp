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

  EXPECT(index.beginDocument({.url = "doc://0", .title = "Doc 0", .input = 0}) == 0u);
  index.ingest("apple");
  index.ingest("banana");
  index.ingest("apple");

  EXPECT(index.beginDocument({.url = "doc://1", .title = "Doc 1", .input = 0}) == 1u);
  index.ingest("apple");

  EXPECT(index.documentMetadata.size() == 2);
  EXPECT(index.currentDocId == 1u);
  EXPECT(index.totalUniqWords == 2u);
  EXPECT(index.offset == 4u);

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
  EXPECT(apple->positions[2] == 3u);

  EXPECT(banana->postings.size() == 1);
  EXPECT(banana->postings[0].docId == 0u);
  EXPECT(banana->postings[0].termFreq == 1u);
  EXPECT(banana->positions.size() == 1);
  EXPECT(banana->positions[0] == 1u);
}

void testIndexDocumentTokenization()
{
  char text[] = "Alpha, beta! ALPHA?? 123 gamma.";
  const size_t words = indexDocument("doc://tokenized", text);

  EXPECT(words == 4u);
  EXPECT(globalIndex.documentMetadata.size() == 1);

  PostingList* alpha = globalIndex.getBucket("alpha");
  PostingList* beta = globalIndex.getBucket("beta");
  PostingList* gamma = globalIndex.getBucket("gamma");

  EXPECT(alpha->postings.size() == 1);
  EXPECT(alpha->postings[0].termFreq == 2u);
  EXPECT(beta->postings.size() == 1);
  EXPECT(beta->postings[0].termFreq == 1u);
  EXPECT(gamma->postings.size() == 1);
  EXPECT(gamma->postings[0].termFreq == 1u);

  EXPECT(alpha->positions.size() == 2);
  EXPECT(alpha->positions[0] == 0u);
  EXPECT(alpha->positions[1] == 2u);
  EXPECT(beta->positions.size() == 1);
  EXPECT(beta->positions[0] == 1u);
  EXPECT(gamma->positions.size() == 1);
  EXPECT(gamma->positions[0] == 3u);
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
  EXPECT(uniqueWordsPlusOne == 2u);

  const size_t dictionaryBytes = static_cast<size_t>(16u + (uniqueWordsPlusOne * 2u * 16u));
  const size_t stringBytes = std::strlen("alpha") + 1u;
  const size_t postingsStart = dictionaryBytes + stringBytes;

  EXPECT(fileSize >= postingsStart + 24u);

  const uint8_t* posting = bytes + postingsStart;
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

} // namespace

int main()
{
  testEncodingHelpers();
  testPostingListIngest();
  testIndexIngestAcrossDocuments();
  testIndexDocumentTokenization();
  testSerializationHeaderAndSkipPointers();
  return 0;
}
