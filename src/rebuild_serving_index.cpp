#include <avengine/search/serving_index_format.h>
#include <avengine/search/serving_index_rebuilder.h>

#include <cstdio>
#include <cstdlib>

namespace
{

void PrintUsage(const char* prog)
{
  std::fprintf(stderr,
               "Usage: %s --input-dir <dir> --output-dir <dir> [--block-size N]\n"
               "\n"
               "Build serving_*.ssi shards from existing NNNNN.sfi and anchor_NNNNN.sfi inputs.\n"
               "Milestone 2 writes valid empty serving shards; later milestones fill postings.\n",
               prog);
}

bool ParseU32(const char* text, uint32_t* out)
{
  if (!text || !out || *text == '\0')
    return false;

  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (!end || *end != '\0' || value == 0 || value > 0xffffffffUL)
    return false;

  *out = static_cast<uint32_t>(value);
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  avengine::search::ServingRebuildOptions options = {};
  options.blockSize = avengine::search::kServingDefaultBlockSize;

  for (int i = 1; i < argc; ++i)
  {
    const avengine::string arg(argv[i]);
    if (arg == "--input-dir" && i + 1 < argc)
    {
      options.inputDirectory = avengine::string(argv[++i]);
    }
    else if (arg == "--output-dir" && i + 1 < argc)
    {
      options.outputDirectory = avengine::string(argv[++i]);
    }
    else if (arg == "--block-size" && i + 1 < argc)
    {
      if (!ParseU32(argv[++i], &options.blockSize))
      {
        std::fprintf(stderr, "invalid --block-size\n");
        return 1;
      }
    }
    else if (arg == "--help" || arg == "-h")
    {
      PrintUsage(argv[0]);
      return 0;
    }
    else
    {
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (options.inputDirectory.size() == 0 || options.outputDirectory.size() == 0)
  {
    PrintUsage(argv[0]);
    return 1;
  }

  avengine::search::ServingRebuildStats stats = {};
  if (!avengine::search::RebuildServingIndexSkeleton(options, &stats))
  {
    std::fprintf(stderr, "[rebuild_serving_index] failed: %s\n", stats.error.cstr());
    return 1;
  }

  std::printf("[rebuild_serving_index] input=%s output=%s primary=%zu anchor=%zu serving=%zu "
              "block_size=%u\n",
              options.inputDirectory.cstr(), options.outputDirectory.cstr(),
              stats.primaryShardCount, stats.anchorShardCount, stats.servingShardCount,
              options.blockSize);
  return 0;
}
