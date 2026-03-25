#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../src/index.h"

#include <avengine/index_stream_reader.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
void appendVarU32(avengine::vector<uint8_t>& out, uint32_t value)
{
  while (value >= 0x80u)
  {
    out.pushBack(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  out.pushBack(static_cast<uint8_t>(value));
}

void appendU64LE(avengine::vector<uint8_t>& out, uint64_t value)
{
  for (int i = 0; i < 8; i++)
    out.pushBack(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
}

uint64_t readU64LE(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; i++)
    value |= static_cast<uint64_t>(ptr[i]) << (i * 8);
  return value;
}
} // namespace

TEST_CASE("parsePostingBlobHeader parses 24-byte header")
{
  avengine::vector<uint8_t> bytes;
  appendU64LE(bytes, 11u);
  appendU64LE(bytes, 12u);
  appendU64LE(bytes, 3u);

  avengine::PostingBlobHeader header = {};
  const uint8_t* fields = nullptr;
  REQUIRE(avengine::parsePostingBlobHeader(bytes.begin(), bytes.end(), &header, &fields));
  CHECK(header.documentFrequency == 11u);
  CHECK(header.collectionFrequency == 12u);
  CHECK(header.skipCount == 3u);
  CHECK(fields == bytes.begin() + 24);
}

TEST_CASE("Dictionary ISR factory methods return owned heap objects")
{
  avengine::Dictionary dict;
  dict.AddOccurrence("alpha", 1);
  dict.AddDocument(1);

  avengine::ISRWord* word = dict.OpenIsr("alpha");
  avengine::ISREndDoc* endDoc = dict.OpenEndDocIsr();

  REQUIRE(word != nullptr);
  REQUIRE(endDoc != nullptr);
  REQUIRE(word->Seek(1) != nullptr);
  REQUIRE(endDoc->Seek(1) != nullptr);
  CHECK(word->GetNumberOfOccurrences() == 1u);
  CHECK(endDoc->GetCurrentDocumentId() == 0u);

  delete word;
  delete endDoc;
}

TEST_CASE("parsePostingBlobHeader matches the current writer format")
{
  Index index;
  index.beginDocument({.url = "doc://serialize", .title = "Serialize", .input = 0});
  index.ingest("alpha");
  index.endDocument();

  char pathTemplate[] = "/tmp/index_stream_reader_test_XXXXXX";
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

  REQUIRE(readU64LE(bytes) == 1u);
  const uint64_t uniqueWordsPlusOne = readU64LE(bytes + 8);
  REQUIRE(uniqueWordsPlusOne == 3u);

  const size_t dictionaryBytes = static_cast<size_t>(16u + (uniqueWordsPlusOne * 2u * 16u));
  const size_t stringBytes = std::strlen("DOCUMENT_END") + 1u + std::strlen("alpha") + 1u;
  const size_t postingsStart = dictionaryBytes + stringBytes;
  REQUIRE(fileSize >= postingsStart + 24u);

  avengine::PostingBlobHeader header = {};
  const uint8_t* fieldStreams = nullptr;
  REQUIRE(avengine::parsePostingBlobHeader(bytes + postingsStart, bytes + fileSize, &header,
                                           &fieldStreams));
  CHECK(header.documentFrequency == 1u);
  CHECK(header.collectionFrequency == 1u);
  CHECK(header.skipCount == 0u);
  CHECK(fieldStreams == bytes + postingsStart + 24u);

  std::free(bytes);
}

TEST_CASE("IndexFieldStreamReader decodes v2 skip sections and position deltas")
{
  avengine::vector<uint8_t> stream;

  // one v2 section: [position_index=2][abs_pos=7][byte_offset=3]
  appendVarU32(stream, 2u);
  appendVarU32(stream, 7u);
  appendVarU32(stream, 3u);

  // positions: 4, 10, 15
  appendVarU32(stream, 4u);
  appendVarU32(stream, 6u);
  appendVarU32(stream, 5u);

  avengine::IndexFieldStreamReader reader;
  REQUIRE(avengine::IndexFieldStreamReader::init(stream.begin(), stream.end(), 1u, 3u,
                                                 avengine::FieldStreamEncoding::V2_SKIP_POINTERS,
                                                 &reader));

  uint32_t position = 0;
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 4u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 10u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 15u);
  CHECK(reader.remainingPositions() == 0u);
  CHECK_FALSE(reader.nextPosition(&position));
}

TEST_CASE("IndexFieldStreamReader decodes v3 decorators and position deltas")
{
  avengine::vector<uint8_t> stream;

  // two v3 decorators:
  // [type=1][payload=2 bytes: AA BB], [type=3][payload=1 byte: 01]
  appendVarU32(stream, 1u);
  appendVarU32(stream, 2u);
  stream.pushBack(0xAAu);
  stream.pushBack(0xBBu);
  appendVarU32(stream, 3u);
  appendVarU32(stream, 1u);
  stream.pushBack(0x01u);

  // positions: 1, 4, 11
  appendVarU32(stream, 1u);
  appendVarU32(stream, 3u);
  appendVarU32(stream, 7u);

  avengine::IndexFieldStreamReader reader;
  REQUIRE(avengine::IndexFieldStreamReader::init(
      stream.begin(), stream.end(), 2u, 3u, avengine::FieldStreamEncoding::V3_DECORATORS, &reader));

  REQUIRE(reader.decorators().size() == 2u);
  CHECK(reader.decorators()[0].type == 1u);
  CHECK(reader.decorators()[0].payloadBytes == 2u);
  CHECK(reader.decorators()[1].type == 3u);
  CHECK(reader.decorators()[1].payloadBytes == 1u);

  uint32_t position = 0;
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 1u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 4u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 11u);
  CHECK(reader.remainingPositions() == 0u);
}
