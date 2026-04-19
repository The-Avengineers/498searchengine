#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

#include <avengine/indexer/indexer.h>
#include <avengine/search/serving_index_rebuilder.h>
#include <avengine/search/serving_shard_source.h>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

avengine::string joinPath(const avengine::string& dir, const char* name)
{
  avengine::string out(dir);
  out.pushBack('/');
  out += avengine::string(name);
  return out;
}

uint16_t countWords(const avengine::string& text)
{
  uint16_t count = 0;
  bool inWord = false;
  for (size_t i = 0; i < text.size(); ++i)
  {
    const char c = text[i];
    const bool isAlpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (isAlpha && !inWord)
    {
      ++count;
      inWord = true;
    }
    else if (!isAlpha)
    {
      inWord = false;
    }
  }
  return count;
}

struct FixtureDoc
{
  const char* url = "";
  const char* title = "";
  const char* description = "";
  const char* terms[8] = {};
  size_t termCount = 0;
};

void writeShard(const avengine::string& path, const FixtureDoc* docs, size_t docCount)
{
  Index index;
  RankingInput rank = {};
  for (size_t i = 0; i < docCount; ++i)
  {
    const avengine::string url(docs[i].url);
    const avengine::string title(docs[i].title);
    const avengine::string description(docs[i].description);
    rank.urlLength = static_cast<uint16_t>(url.size());
    rank.titleWordCount = countWords(title);
    index.beginDocument({.url = url, .title = title, .description = description, .input = rank});
    for (size_t t = 0; t < docs[i].termCount; ++t)
      index.ingest(docs[i].terms[t]);
    index.endDocument();
  }

  const int fd = open(path.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  REQUIRE(fd >= 0);
  REQUIRE(index.serialize(fd) == false);
  CHECK(close(fd) == 0);
}

void cleanupPath(const avengine::string& path)
{
  if (unlink(path.cstr()) != 0)
    CHECK(errno == ENOENT);
}

} // namespace

TEST_CASE("serving shard source loads into memory and streams block postings")
{
  char rootTemplate[] = "/tmp/serving_shard_source_test_XXXXXX";
  char* rootRaw = mkdtemp(rootTemplate);
  REQUIRE(rootRaw != nullptr);
  const avengine::string root(rootRaw);
  const avengine::string input = joinPath(root, "input");
  const avengine::string output = joinPath(root, "output");
  REQUIRE(mkdir(input.cstr(), 0755) == 0);
  REQUIRE(mkdir(output.cstr(), 0755) == 0);

  const FixtureDoc primaryDocs[] = {
      {"doc://one", "One", "first", {"alpha", "alpha", "#alpha"}, 3u},
      {"doc://two", "Two", "second", {"alpha", "beta"}, 2u},
  };
  writeShard(joinPath(input, "00000.sfi"), primaryDocs,
             sizeof(primaryDocs) / sizeof(primaryDocs[0]));

  const FixtureDoc anchorDocs[] = {
      {"doc://one", "One", "", {"alpha"}, 1u},
  };
  writeShard(joinPath(input, "anchor_00000.sfi"), anchorDocs,
             sizeof(anchorDocs) / sizeof(anchorDocs[0]));

  avengine::search::ServingRebuildOptions options;
  options.inputDirectory = input;
  options.outputDirectory = output;
  options.blockSize = 2u;
  avengine::search::ServingRebuildStats stats = {};
  REQUIRE(avengine::search::RebuildServingIndex(options, &stats));

  const avengine::string servingPath = joinPath(output, "serving_00000.ssi");
  avengine::search::ServingShardSource source(servingPath);
  REQUIRE(source.IsLoaded());
  CHECK(source.DocumentCount() == 2u);
  CHECK(source.GetDocumentURL(0) == avengine::string("doc://one"));
  CHECK(source.GetDocumentTitle(1) == avengine::string("Two"));

  avengine::search::ServingTermView alpha;
  REQUIRE(source.LookupTerm(avengine::string("alpha"), &alpha));
  REQUIRE(alpha.IsValid());
  CHECK(alpha.termInfo->documentFrequency == 2u);

  const avengine::search::ServingTierInfoV1* strongTier = nullptr;
  for (uint32_t i = 0; i < alpha.tierCount; ++i)
  {
    if (alpha.tiers[i].tierId ==
        static_cast<uint32_t>(avengine::search::ServingTierId::Strong))
    {
      strongTier = &alpha.tiers[i];
    }
  }
  REQUIRE(strongTier != nullptr);
  REQUIRE(strongTier->blockCount == 1u);

  const avengine::search::ServingBlockInfoV1* block = source.GetBlockInfo(*strongTier, 0);
  REQUIRE(block != nullptr);
  avengine::search::ServingPostingBlockCursor cursor;
  REQUIRE(source.OpenBlockCursor(*block, strongTier->tierId, &cursor));

  avengine::search::ServingDecodedPosting posting;
  REQUIRE(cursor.Next(&posting));
  CHECK(posting.docId == 0u);
  CHECK(posting.bodyTf == 2u);
  CHECK(posting.titleTf == 1u);
  CHECK(posting.anchorTf == 1u);
  CHECK(!cursor.Next(&posting));
  CHECK(cursor.IsValid());

  avengine::vector<avengine::search::ServingDecodedPosting> decoded;
  REQUIRE(source.DecodeTermPostings(avengine::string("alpha"), &decoded));
  CHECK(decoded.size() == 2u);

  cleanupPath(joinPath(input, "00000.sfi"));
  cleanupPath(joinPath(input, "anchor_00000.sfi"));
  cleanupPath(servingPath);
  CHECK(rmdir(input.cstr()) == 0);
  CHECK(rmdir(output.cstr()) == 0);
  CHECK(rmdir(root.cstr()) == 0);
}
