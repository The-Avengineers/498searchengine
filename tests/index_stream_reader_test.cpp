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
  index.beginDocument(
      {.url = "doc://serialize", .title = "Serialize", .description = "", .input = 0});
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

TEST_CASE("IndexShardSource preserves ranking input")
{
  Index index;
  const RankingInput expected = {
      .domainDepth = 3u, .depth = 7u, .urlLength = 31u, .titleWordCount = 4u, .seedSource = 9u};
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
  CHECK(info->rankingInput.domainDepth == expected.domainDepth);
  CHECK(info->rankingInput.depth == expected.depth);
  CHECK(info->rankingInput.urlLength == expected.urlLength);
  CHECK(info->rankingInput.titleWordCount == expected.titleWordCount);
  CHECK(info->rankingInput.seedSource == expected.seedSource);

  CHECK(unlink(pathTemplate) == 0);
}

TEST_CASE("ISRWordLazy seek and next match eager ISRWord")
{
  // Build a single-document index where "beta" appears 9 times.
  // floor(sqrt(9)) = 3, so skip pointers are generated at position indices 3 and 6.
  Index index;
  index.beginDocument({.url = "doc://lazy", .title = "Lazy", .description = "", .input = 0});
  for (int i = 0; i < 9; ++i)
    index.ingest("beta");
  index.endDocument();

  char pathTemplate[] = "/tmp/isr_word_lazy_test_XXXXXX";
  const int fd = mkstemp(pathTemplate);
  REQUIRE(fd >= 0);
  REQUIRE(index.serialize(fd) == false);
  CHECK(close(fd) == 0);

  avengine::search::IndexShardSource shard(pathTemplate);
  REQUIRE(shard.IsLoaded());

  // --- Sequential Next() ---
  // ISRWordLazy starts positioned at the first occurrence after construction.
  {
    avengine::ISRWord* isr = shard.OpenTermIsr(avengine::string("beta"));
    REQUIRE(isr != nullptr);

    // Positions are 1-based; offsets 0..8 become locations 1..9.
    for (avengine::Location expected = 1; expected <= 9; ++expected)
    {
      avengine::Post* p = isr->GetCurrentPost();
      REQUIRE(p != nullptr);
      CHECK(p->GetStartLocation() == expected);
      if (expected < 9)
        isr->Next();
    }
    CHECK(isr->Next() == nullptr); // exhausted
    delete isr;
  }

  // --- Seek to each location individually ---
  {
    for (avengine::Location target = 1; target <= 9; ++target)
    {
      avengine::ISRWord* isr = shard.OpenTermIsr(avengine::string("beta"));
      REQUIRE(isr != nullptr);
      avengine::Post* p = isr->Seek(target);
      REQUIRE(p != nullptr);
      CHECK(p->GetStartLocation() == target);
      delete isr;
    }
  }

  // --- Seek past all positions returns nullptr ---
  {
    avengine::ISRWord* isr = shard.OpenTermIsr(avengine::string("beta"));
    REQUIRE(isr != nullptr);
    CHECK(isr->Seek(10) == nullptr);
    delete isr;
  }

  // --- Seek backwards (target < current position) restarts correctly ---
  {
    avengine::ISRWord* isr = shard.OpenTermIsr(avengine::string("beta"));
    REQUIRE(isr != nullptr);
    // Jump forward using a skip pointer (target=7 uses the i=6 skip).
    avengine::Post* p = isr->Seek(7);
    REQUIRE(p != nullptr);
    CHECK(p->GetStartLocation() == 7u);
    // Now seek backwards to 2 (below any skip pointer boundary).
    p = isr->Seek(2);
    REQUIRE(p != nullptr);
    CHECK(p->GetStartLocation() == 2u);
    // Continue forward from there.
    p = isr->Next();
    REQUIRE(p != nullptr);
    CHECK(p->GetStartLocation() == 3u);
    delete isr;
  }

  // --- Seek to skip boundary exactly (location 4 = raw positionValue 3 + 1) ---
  {
    avengine::ISRWord* isr = shard.OpenTermIsr(avengine::string("beta"));
    REQUIRE(isr != nullptr);
    avengine::Post* p = isr->Seek(4);
    REQUIRE(p != nullptr);
    CHECK(p->GetStartLocation() == 4u);
    p = isr->Next();
    REQUIRE(p != nullptr);
    CHECK(p->GetStartLocation() == 5u);
    delete isr;
  }

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
