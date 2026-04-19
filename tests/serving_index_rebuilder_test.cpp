#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

#include <avengine/search/serving_index_format.h>
#include <avengine/search/serving_index_rebuilder.h>
#include <avengine/indexer/indexer.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
using avengine::vector;

avengine::string joinPath(const avengine::string& dir, const char* name)
{
  avengine::string out(dir);
  out.pushBack('/');
  out += avengine::string(name);
  return out;
}

void writeTinyFile(const avengine::string& path)
{
  const int fd = open(path.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  REQUIRE(fd >= 0);
  const char byte = 'x';
  REQUIRE(write(fd, &byte, 1) == 1);
  CHECK(close(fd) == 0);
}

avengine::search::ServingShardHeaderV1 readServingHeader(const avengine::string& path)
{
  avengine::search::ServingShardHeaderV1 header = {};
  const int fd = open(path.cstr(), O_RDONLY);
  REQUIRE(fd >= 0);
  ssize_t total = 0;
  while (static_cast<size_t>(total) < sizeof(header))
  {
    const ssize_t n = read(fd, reinterpret_cast<char*>(&header) + total,
                           sizeof(header) - static_cast<size_t>(total));
    REQUIRE(n > 0);
    total += n;
  }
  CHECK(close(fd) == 0);
  return header;
}

void cleanupPath(const avengine::string& path)
{
  if (unlink(path.cstr()) != 0)
    CHECK(errno == ENOENT);
}

vector<uint8_t> readFileBytes(const avengine::string& path)
{
  struct stat st = {};
  REQUIRE(stat(path.cstr(), &st) == 0);
  vector<uint8_t> bytes(static_cast<size_t>(st.st_size));
  const int fd = open(path.cstr(), O_RDONLY);
  REQUIRE(fd >= 0);
  size_t total = 0;
  while (total < bytes.size())
  {
    const ssize_t n = read(fd, bytes.begin() + total, bytes.size() - total);
    REQUIRE(n > 0);
    total += static_cast<size_t>(n);
  }
  CHECK(close(fd) == 0);
  return bytes;
}

template <typename T>
const T* sectionAt(const vector<uint8_t>& bytes, uint64_t offset, uint64_t count)
{
  REQUIRE(offset <= bytes.size());
  REQUIRE(count <= (bytes.size() - static_cast<size_t>(offset)) / sizeof(T));
  return reinterpret_cast<const T*>(bytes.begin() + static_cast<size_t>(offset));
}

bool termEquals(const vector<uint8_t>& bytes,
                const avengine::search::ServingShardHeaderV1& header,
                const avengine::search::ServingLexiconEntryV1& entry, const char* term)
{
  size_t len = 0;
  while (term[len] != '\0')
    ++len;
  if (entry.termLength != len)
    return false;
  const size_t start = static_cast<size_t>(header.termBytesOffset + entry.termOffset);
  if (start + len > bytes.size())
    return false;
  return std::memcmp(bytes.begin() + start, term, len) == 0;
}

const avengine::search::ServingLexiconEntryV1* findServingTerm(
    const vector<uint8_t>& bytes, const avengine::search::ServingShardHeaderV1& header,
    const char* term)
{
  const auto* lexicon = sectionAt<avengine::search::ServingLexiconEntryV1>(
      bytes, header.lexiconOffset, header.lexiconEntryCount);
  for (uint64_t i = 0; i < header.lexiconEntryCount; ++i)
  {
    if (termEquals(bytes, header, lexicon[i], term))
      return &lexicon[i];
  }
  return nullptr;
}

uint32_t readVarU32(const vector<uint8_t>& bytes, size_t* cursor, size_t end)
{
  uint32_t value = 0;
  uint32_t shift = 0;
  while (*cursor < end && shift < 35)
  {
    const uint8_t byte = bytes[*cursor];
    ++(*cursor);
    value |= static_cast<uint32_t>(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0)
      return value;
    shift += 7u;
  }
  FAIL("truncated varint");
  return 0;
}

struct DecodedServingPosting
{
  uint32_t docId = 0;
  uint8_t mask = 0;
  uint32_t bodyTf = 0;
  uint32_t titleTf = 0;
  uint32_t anchorTf = 0;
};

vector<DecodedServingPosting> decodeBlockPostings(
    const vector<uint8_t>& bytes, const avengine::search::ServingShardHeaderV1& header,
    const avengine::search::ServingBlockInfoV1& block)
{
  vector<DecodedServingPosting> postings;
  size_t cursor = static_cast<size_t>(header.postingsOffset + block.payloadOffset);
  const size_t end = cursor + static_cast<size_t>(block.payloadLength);
  REQUIRE(end <= bytes.size());

  uint32_t previousDocId = 0;
  for (uint32_t i = 0; i < block.docCount; ++i)
  {
    DecodedServingPosting posting;
    const uint32_t delta = readVarU32(bytes, &cursor, end);
    posting.docId = i == 0 ? delta : previousDocId + delta;
    previousDocId = posting.docId;
    REQUIRE(cursor < end);
    posting.mask = bytes[cursor++];
    if ((posting.mask & avengine::search::kServingFieldBody) != 0)
      posting.bodyTf = readVarU32(bytes, &cursor, end);
    if ((posting.mask & avengine::search::kServingFieldTitle) != 0)
      posting.titleTf = readVarU32(bytes, &cursor, end);
    if ((posting.mask & avengine::search::kServingFieldAnchor) != 0)
      posting.anchorTf = readVarU32(bytes, &cursor, end);
    postings.pushBack(posting);
  }
  CHECK(cursor == end);
  return postings;
}

struct ShardFixtureDoc
{
  DocumentMetadata metadata;
  const char* terms[8];
  size_t termCount = 0;
};

void writeIndexShard(const avengine::string& path, const ShardFixtureDoc* docs, size_t docCount)
{
  Index index;
  for (size_t docIndex = 0; docIndex < docCount; ++docIndex)
  {
    index.beginDocument(docs[docIndex].metadata);
    for (size_t termIndex = 0; termIndex < docs[docIndex].termCount; ++termIndex)
      index.ingest(docs[docIndex].terms[termIndex]);
    index.endDocument();
  }

  const int fd = open(path.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  REQUIRE(fd >= 0);
  REQUIRE(index.serialize(fd) == false);
  CHECK(close(fd) == 0);
}

} // namespace

TEST_CASE("serving rebuild skeleton discovers primary and anchor shards")
{
  char rootTemplate[] = "/tmp/serving_rebuilder_test_XXXXXX";
  char* rootRaw = mkdtemp(rootTemplate);
  REQUIRE(rootRaw != nullptr);
  const avengine::string root(rootRaw);
  const avengine::string input = joinPath(root, "input");
  const avengine::string output = joinPath(root, "output");
  REQUIRE(mkdir(input.cstr(), 0755) == 0);
  REQUIRE(mkdir(output.cstr(), 0755) == 0);

  writeTinyFile(joinPath(input, "00002.sfi"));
  writeTinyFile(joinPath(input, "00001.sfi"));
  writeTinyFile(joinPath(input, "anchor_00003.sfi"));
  writeTinyFile(joinPath(input, "ignored.sfi"));

  avengine::search::ServingRebuildOptions options;
  options.inputDirectory = input;
  options.outputDirectory = output;
  options.blockSize = 64u;

  avengine::search::ServingRebuildStats stats = {};
  REQUIRE(avengine::search::RebuildServingIndexSkeleton(options, &stats));
  CHECK(stats.primaryShardCount == 2u);
  CHECK(stats.anchorShardCount == 1u);
  CHECK(stats.servingShardCount == 2u);
  CHECK(stats.error.size() == 0u);

  const avengine::string servingOne = joinPath(output, "serving_00001.ssi");
  const avengine::string servingTwo = joinPath(output, "serving_00002.ssi");
  struct stat st = {};
  REQUIRE(stat(servingOne.cstr(), &st) == 0);
  CHECK(st.st_size == static_cast<off_t>(sizeof(avengine::search::ServingShardHeaderV1)));
  REQUIRE(stat(servingTwo.cstr(), &st) == 0);
  CHECK(st.st_size == static_cast<off_t>(sizeof(avengine::search::ServingShardHeaderV1)));

  const avengine::search::ServingShardHeaderV1 header = readServingHeader(servingTwo);
  CHECK(avengine::search::IsServingShardMagic(header.magic));
  CHECK(avengine::search::IsSupportedServingShardVersion(header.version));
  CHECK(header.headerBytes == sizeof(avengine::search::ServingShardHeaderV1));
  CHECK(header.shardId == 2u);
  CHECK(header.blockSize == 64u);
  CHECK(header.lexiconEntryCount == 0u);
  CHECK(header.termCount == 0u);
  CHECK(header.tierCount == 0u);
  CHECK(header.blockCount == 0u);
  CHECK(header.postingsLength == 0u);

  cleanupPath(joinPath(input, "00001.sfi"));
  cleanupPath(joinPath(input, "00002.sfi"));
  cleanupPath(joinPath(input, "anchor_00003.sfi"));
  cleanupPath(joinPath(input, "ignored.sfi"));
  cleanupPath(servingOne);
  cleanupPath(servingTwo);
  CHECK(rmdir(input.cstr()) == 0);
  CHECK(rmdir(output.cstr()) == 0);
  CHECK(rmdir(root.cstr()) == 0);
}

TEST_CASE("serving rebuild skeleton fails when no primary shards exist")
{
  char rootTemplate[] = "/tmp/serving_rebuilder_empty_XXXXXX";
  char* rootRaw = mkdtemp(rootTemplate);
  REQUIRE(rootRaw != nullptr);
  const avengine::string root(rootRaw);
  const avengine::string input = joinPath(root, "input");
  const avengine::string output = joinPath(root, "output");
  REQUIRE(mkdir(input.cstr(), 0755) == 0);
  REQUIRE(mkdir(output.cstr(), 0755) == 0);

  writeTinyFile(joinPath(input, "anchor_00000.sfi"));

  avengine::search::ServingRebuildOptions options;
  options.inputDirectory = input;
  options.outputDirectory = output;

  avengine::search::ServingRebuildStats stats = {};
  CHECK(!avengine::search::RebuildServingIndexSkeleton(options, &stats));
  CHECK(stats.primaryShardCount == 0u);
  CHECK(stats.anchorShardCount == 1u);
  CHECK(stats.servingShardCount == 0u);
  CHECK(stats.error.size() > 0u);

  cleanupPath(joinPath(input, "anchor_00000.sfi"));
  CHECK(rmdir(input.cstr()) == 0);
  CHECK(rmdir(output.cstr()) == 0);
  CHECK(rmdir(root.cstr()) == 0);
}

TEST_CASE("serving rebuild writes merged tiered postings from primary and anchor shards")
{
  char rootTemplate[] = "/tmp/serving_rebuilder_full_XXXXXX";
  char* rootRaw = mkdtemp(rootTemplate);
  REQUIRE(rootRaw != nullptr);
  const avengine::string root(rootRaw);
  const avengine::string input = joinPath(root, "input");
  const avengine::string output = joinPath(root, "output");
  REQUIRE(mkdir(input.cstr(), 0755) == 0);
  REQUIRE(mkdir(output.cstr(), 0755) == 0);

  RankingInput rank = {};
  const ShardFixtureDoc primaryDocs[] = {
      {{.url = "doc://one", .title = "One", .description = "first", .input = rank},
       {"alpha", "alpha", "beta", "#alpha"},
       4u},
      {{.url = "doc://two", .title = "Two", .description = "second", .input = rank},
       {"alpha", "gamma", "gamma"},
       3u},
  };
  writeIndexShard(joinPath(input, "00000.sfi"), primaryDocs,
                  sizeof(primaryDocs) / sizeof(primaryDocs[0]));

  const ShardFixtureDoc anchorDocs[] = {
      {{.url = "doc://one", .title = "One", .description = "", .input = rank},
       {"alpha", "alpha"},
       2u},
      {{.url = "doc://two", .title = "Two", .description = "", .input = rank},
       {"beta"},
       1u},
  };
  writeIndexShard(joinPath(input, "anchor_00000.sfi"), anchorDocs,
                  sizeof(anchorDocs) / sizeof(anchorDocs[0]));

  avengine::search::ServingRebuildOptions options;
  options.inputDirectory = input;
  options.outputDirectory = output;
  options.blockSize = 2u;

  avengine::search::ServingRebuildStats stats = {};
  REQUIRE(avengine::search::RebuildServingIndex(options, &stats));
  CHECK(stats.primaryShardCount == 1u);
  CHECK(stats.anchorShardCount == 1u);
  CHECK(stats.servingShardCount == 1u);

  const avengine::string servingPath = joinPath(output, "serving_00000.ssi");
  const vector<uint8_t> bytes = readFileBytes(servingPath);
  REQUIRE(bytes.size() > sizeof(avengine::search::ServingShardHeaderV1));

  const auto* header = reinterpret_cast<const avengine::search::ServingShardHeaderV1*>(bytes.begin());
  CHECK(avengine::search::IsServingShardMagic(header->magic));
  CHECK(header->documentCount == 2u);
  CHECK(header->lexiconEntryCount == 3u);
  CHECK(header->termCount == 3u);
  CHECK(header->blockSize == 2u);
  CHECK(header->docLengthLength == 2u * sizeof(uint32_t));

  const auto* docLengths = sectionAt<uint32_t>(bytes, header->docLengthOffset, 2u);
  CHECK(docLengths[0] == 4u);
  CHECK(docLengths[1] == 4u);

  const auto* alphaLex = findServingTerm(bytes, *header, "alpha");
  REQUIRE(alphaLex != nullptr);
  const auto* termInfos = sectionAt<avengine::search::ServingTermInfoV1>(
      bytes, header->termInfoOffset, header->termCount);
  const auto& alphaInfo = termInfos[alphaLex->termInfoIndex];
  CHECK(alphaInfo.documentFrequency == 2u);
  CHECK(alphaInfo.collectionFrequency == 6u);
  CHECK(alphaInfo.bodyDocumentFrequency == 2u);
  CHECK(alphaInfo.titleDocumentFrequency == 1u);
  CHECK(alphaInfo.anchorDocumentFrequency == 1u);
  CHECK(alphaInfo.tierCount == 2u);

  const auto* tiers = sectionAt<avengine::search::ServingTierInfoV1>(
      bytes, header->tierInfoOffset, header->tierCount);
  const auto* blocks = sectionAt<avengine::search::ServingBlockInfoV1>(
      bytes, header->blockInfoOffset, header->blockCount);
  const auto& alphaStrong = tiers[alphaInfo.tierInfoIndex];
  CHECK(alphaStrong.tierId == static_cast<uint32_t>(avengine::search::ServingTierId::Strong));
  REQUIRE(alphaStrong.blockCount == 1u);
  auto alphaStrongPostings = decodeBlockPostings(bytes, *header, blocks[alphaStrong.blockIndex]);
  REQUIRE(alphaStrongPostings.size() == 1u);
  CHECK(alphaStrongPostings[0].docId == 0u);
  CHECK(alphaStrongPostings[0].bodyTf == 2u);
  CHECK(alphaStrongPostings[0].titleTf == 1u);
  CHECK(alphaStrongPostings[0].anchorTf == 2u);
  CHECK(alphaStrongPostings[0].mask ==
        (avengine::search::kServingFieldBody | avengine::search::kServingFieldTitle |
         avengine::search::kServingFieldAnchor));

  const auto& alphaRecall = tiers[alphaInfo.tierInfoIndex + 1u];
  CHECK(alphaRecall.tierId == static_cast<uint32_t>(avengine::search::ServingTierId::Recall));
  auto alphaRecallPostings = decodeBlockPostings(bytes, *header, blocks[alphaRecall.blockIndex]);
  REQUIRE(alphaRecallPostings.size() == 1u);
  CHECK(alphaRecallPostings[0].docId == 1u);
  CHECK(alphaRecallPostings[0].bodyTf == 1u);
  CHECK(alphaRecallPostings[0].titleTf == 0u);
  CHECK(alphaRecallPostings[0].anchorTf == 0u);

  const auto* gammaLex = findServingTerm(bytes, *header, "gamma");
  REQUIRE(gammaLex != nullptr);
  const auto& gammaInfo = termInfos[gammaLex->termInfoIndex];
  REQUIRE(gammaInfo.tierCount == 1u);
  const auto& gammaTier = tiers[gammaInfo.tierInfoIndex];
  CHECK(gammaTier.tierId == static_cast<uint32_t>(avengine::search::ServingTierId::Medium));
  auto gammaPostings = decodeBlockPostings(bytes, *header, blocks[gammaTier.blockIndex]);
  REQUIRE(gammaPostings.size() == 1u);
  CHECK(gammaPostings[0].docId == 1u);
  CHECK(gammaPostings[0].bodyTf == 2u);

  const auto* betaLex = findServingTerm(bytes, *header, "beta");
  REQUIRE(betaLex != nullptr);
  const auto& betaInfo = termInfos[betaLex->termInfoIndex];
  CHECK(betaInfo.documentFrequency == 2u);
  CHECK(betaInfo.anchorDocumentFrequency == 1u);

  cleanupPath(joinPath(input, "00000.sfi"));
  cleanupPath(joinPath(input, "anchor_00000.sfi"));
  cleanupPath(servingPath);
  CHECK(rmdir(input.cstr()) == 0);
  CHECK(rmdir(output.cstr()) == 0);
  CHECK(rmdir(root.cstr()) == 0);
}
