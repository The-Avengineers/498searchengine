#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

#include <avengine/indexer/indexer.h>
#include <avengine/search/legacy_shard_reader.h>

#include <fcntl.h>
#include <unistd.h>

namespace
{

avengine::string writeFixtureShard()
{
  Index index;

  RankingInput firstRank = {
      .domainDepth = 1u, .depth = 2u, .urlLength = 12u, .titleWordCount = 1u, .seedSource = 3u};
  index.beginDocument(
      {.url = "doc://one", .title = "One", .description = "first doc", .input = firstRank});
  index.ingest("alpha");
  index.ingest("alpha");
  index.ingest("beta");
  index.ingest("#alpha");
  index.endDocument();

  RankingInput secondRank = {
      .domainDepth = 4u, .depth = 5u, .urlLength = 12u, .titleWordCount = 1u, .seedSource = 6u};
  index.beginDocument(
      {.url = "doc://two", .title = "Two", .description = "second doc", .input = secondRank});
  index.ingest("alpha");
  index.ingest("gamma");
  index.endDocument();

  char pathTemplate[] = "/tmp/legacy_shard_reader_XXXXXX";
  const int fd = mkstemp(pathTemplate);
  REQUIRE(fd >= 0);
  REQUIRE(index.serialize(fd) == false);
  CHECK(close(fd) == 0);
  return avengine::string(pathTemplate);
}

const avengine::search::LegacyTermInfo* findTerm(const avengine::search::LegacyShardReader& reader,
                                                 const char* term)
{
  for (size_t i = 0; i < reader.TermCount(); ++i)
  {
    const avengine::search::LegacyTermInfo* info = reader.GetTerm(i);
    if (info && info->term == avengine::string(term))
      return info;
  }
  return nullptr;
}

void removeFile(const avengine::string& path)
{
  CHECK(unlink(path.cstr()) == 0);
}

} // namespace

TEST_CASE("legacy shard reader enumerates terms and document metadata")
{
  const avengine::string path = writeFixtureShard();

  avengine::search::LegacyShardReader reader(path);
  REQUIRE(reader.IsLoaded());
  CHECK(reader.DocumentCount() == 2u);
  CHECK(reader.TermCount() == 5u); // alpha, beta, gamma, #alpha, DOCUMENT_END

  const avengine::search::LegacyTermInfo* alpha = findTerm(reader, "alpha");
  REQUIRE(alpha != nullptr);
  CHECK(alpha->documentFrequency == 2u);
  CHECK(alpha->collectionFrequency == 3u);

  const avengine::search::LegacyTermInfo* titleAlpha = findTerm(reader, "#alpha");
  REQUIRE(titleAlpha != nullptr);
  CHECK(titleAlpha->documentFrequency == 1u);
  CHECK(titleAlpha->collectionFrequency == 1u);

  const avengine::search::LegacyDocumentMetadata* first = reader.GetDocument(0);
  REQUIRE(first != nullptr);
  CHECK(first->docId == 0u);
  CHECK(first->url == avengine::string("doc://one"));
  CHECK(first->title == avengine::string("One"));
  CHECK(first->description == avengine::string("first doc"));
  CHECK(first->startLocation == 1u);
  CHECK(first->endLocation == 4u);
  CHECK(first->docLength == 4u);
  CHECK(first->rankingInput.domainDepth == 1u);
  CHECK(first->rankingInput.depth == 2u);
  CHECK(first->rankingInput.seedSource == 3u);

  const avengine::search::LegacyDocumentMetadata* second = reader.GetDocument(1);
  REQUIRE(second != nullptr);
  CHECK(second->docId == 1u);
  CHECK(second->url == avengine::string("doc://two"));
  CHECK(second->startLocation == 5u);
  CHECK(second->endLocation == 7u);
  CHECK(second->docLength == 3u);
  CHECK(second->rankingInput.domainDepth == 4u);

  removeFile(path);
}

TEST_CASE("legacy shard reader reconstructs doc-oriented term frequencies")
{
  const avengine::string path = writeFixtureShard();

  avengine::search::LegacyShardReader reader(path);
  REQUIRE(reader.IsLoaded());

  avengine::vector<avengine::search::LegacyDocTermStats> stats;
  REQUIRE(reader.DecodeTermDocumentStats(avengine::string("alpha"), &stats));
  REQUIRE(stats.size() == 2u);
  CHECK(stats[0].docId == 0u);
  CHECK(stats[0].termFrequency == 2u);
  CHECK(stats[1].docId == 1u);
  CHECK(stats[1].termFrequency == 1u);

  REQUIRE(reader.DecodeTermDocumentStats(avengine::string("#alpha"), &stats));
  REQUIRE(stats.size() == 1u);
  CHECK(stats[0].docId == 0u);
  CHECK(stats[0].termFrequency == 1u);

  REQUIRE(reader.DecodeTermDocumentStats(avengine::string("gamma"), &stats));
  REQUIRE(stats.size() == 1u);
  CHECK(stats[0].docId == 1u);
  CHECK(stats[0].termFrequency == 1u);

  CHECK(!reader.DecodeTermDocumentStats(avengine::string("missing"), &stats));
  CHECK(stats.size() == 0u);

  removeFile(path);
}

TEST_CASE("legacy shard reader rejects corrupt magic")
{
  char pathTemplate[] = "/tmp/legacy_shard_bad_XXXXXX";
  const int fd = mkstemp(pathTemplate);
  REQUIRE(fd >= 0);

  const uint64_t zeros[5] = {};
  REQUIRE(write(fd, zeros, sizeof(zeros)) == static_cast<ssize_t>(sizeof(zeros)));
  CHECK(close(fd) == 0);

  avengine::search::LegacyShardReader reader{avengine::string(pathTemplate)};
  CHECK(!reader.IsLoaded());
  CHECK(reader.GetError().size() > 0u);

  removeFile(avengine::string(pathTemplate));
}
