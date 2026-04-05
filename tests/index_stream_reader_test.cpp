#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/codec.h>
#include <avengine/index_stream_reader.h>
#include <avengine/indexer/indexer.h>
#include <avengine/search/index_shard_source.h>

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

  avengine::Reader fileHeader(bytes, 16u);
  auto dcVal = fileHeader.readU64();
  auto uwVal = fileHeader.readU64();
  REQUIRE(dcVal == 1u);
  REQUIRE(uwVal == 3u);
  const uint64_t uniqueWordsPlusOne = uwVal;

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

TEST_CASE("IndexShardSource preserves packed ranking input")
{
  Index index;
  const RankingInput expected = ranking_input::Pack(7u, 31u, 4u);
  index.beginDocument({.url = "doc://static", .title = "Static Title", .input = expected});
  index.ingest("alpha");
  index.endDocument();

  char pathTemplate[] = "/tmp/index_stream_reader_rank_input_XXXXXX";
  const int fd = mkstemp(pathTemplate);
  REQUIRE(fd >= 0);

  CHECK(index.serialize(fd) == false);
  CHECK(close(fd) == 0);

  avengine::search::IndexShardSource shard(pathTemplate);
  REQUIRE(shard.IsLoaded());
  const avengine::DocumentInfo* info = shard.GetDocumentInfo(0);
  REQUIRE(info != nullptr);
  CHECK(info->rankingInput == expected);
  CHECK(ranking_input::Depth(info->rankingInput) == 7u);
  CHECK(ranking_input::UrlLength(info->rankingInput) == 31u);
  CHECK(ranking_input::TitleWordCount(info->rankingInput) == 4u);

  CHECK(unlink(pathTemplate) == 0);
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
