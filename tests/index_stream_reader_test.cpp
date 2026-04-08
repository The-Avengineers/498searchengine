#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/codec.h>
#include <avengine/index_stream_reader.h>
#include <avengine/indexer/indexer.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

TEST_CASE("parsePostingBlobHeader parses 24-byte header")
{
  avengine::Writer w;
  w.writeU64(11u);
  w.writeU64(12u);
  w.writeU64(3u);

  avengine::PostingBlobHeader header = {};
  const uint8_t* fields = nullptr;
  REQUIRE(avengine::parsePostingBlobHeader(w.data(), w.data() + w.size(), &header, &fields));
  CHECK(header.documentFrequency == 11u);
  CHECK(header.collectionFrequency == 12u);
  CHECK(header.skipCount == 3u);
  CHECK(fields == w.data() + 24);
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

  avengine::Reader fileHeader(bytes, fileSize);
  auto magicVal = fileHeader.readU64();
  auto dcVal = fileHeader.readU64();
  auto uwVal = fileHeader.readU64();
  auto tokenOffsetVal = fileHeader.readU64();
  auto rankingInputSizeVal = fileHeader.readU64();
  REQUIRE(magicVal == 0x008FB8EF84979FF0ULL);
  REQUIRE(dcVal == 1u);
  REQUIRE(uwVal == 2u);
  REQUIRE(tokenOffsetVal == 2u);
  REQUIRE(rankingInputSizeVal == sizeof(RankingInput));

  uint64_t alphaPostingOffset = UINT64_MAX;
  for (uint64_t i = 0; i < uwVal; ++i)
  {
    avengine::string word = fileHeader.readString();
    auto positionsCount = fileHeader.readU64();
    auto postingsCount = fileHeader.readU64();
    auto postingOffset = fileHeader.readU64();

    if (word == avengine::string("alpha"))
    {
      CHECK(positionsCount == 1u);
      CHECK(postingsCount == 1u);
      alphaPostingOffset = postingOffset;
    }
  }

  REQUIRE(alphaPostingOffset != UINT64_MAX);

  const size_t postingsStart = static_cast<size_t>(fileHeader.cursor() - bytes);
  const size_t alphaStart = postingsStart + static_cast<size_t>(alphaPostingOffset);
  REQUIRE(fileSize >= alphaStart + 24u);

  avengine::PostingBlobHeader header = {};
  const uint8_t* fieldStreams = nullptr;
  REQUIRE(avengine::parsePostingBlobHeader(bytes + alphaStart, bytes + fileSize, &header,
                                           &fieldStreams));
  CHECK(header.documentFrequency == 1u);
  CHECK(header.collectionFrequency == 1u);
  CHECK(header.skipCount == 0u);
  CHECK(fieldStreams == bytes + alphaStart + 24u);

  std::free(bytes);
}

TEST_CASE("IndexFieldStreamReader decodes v2 skip sections and position deltas")
{
  avengine::Writer w;
  w.writeVarU32(2u);
  w.writeVarU32(7u);
  w.writeVarU32(3u);

  w.writeVarU32(4u);
  w.writeVarU32(6u);
  w.writeVarU32(5u);

  avengine::IndexFieldStreamReader reader;
  REQUIRE(avengine::IndexFieldStreamReader::init(w.data(), w.data() + w.size(), 1u, 3u,
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
  avengine::Writer w;
  w.writeVarU32(1u);
  w.writeVarU32(2u);
  w.writeBytes("\xAA\xBB", 2);
  w.writeVarU32(3u);
  w.writeVarU32(1u);
  w.writeBytes("\x01", 1);

  w.writeVarU32(1u);
  w.writeVarU32(3u);
  w.writeVarU32(7u);

  avengine::IndexFieldStreamReader reader;
  REQUIRE(avengine::IndexFieldStreamReader::init(w.data(), w.data() + w.size(), 2u, 3u,
                                                 avengine::FieldStreamEncoding::V3_DECORATORS,
                                                 &reader));

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
