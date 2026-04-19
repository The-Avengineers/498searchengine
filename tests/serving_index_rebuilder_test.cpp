#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

#include <avengine/search/serving_index_format.h>
#include <avengine/search/serving_index_rebuilder.h>

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
