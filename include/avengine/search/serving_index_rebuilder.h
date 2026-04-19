#pragma once

#include <avengine/string.h>
#include <avengine/vector.h>

#include <cstddef>
#include <cstdint>

namespace avengine
{
namespace search
{

struct ServingRebuildOptions
{
  string inputDirectory;
  string outputDirectory;
  uint32_t blockSize = 128;
};

struct ServingRebuildStats
{
  size_t primaryShardCount = 0;
  size_t anchorShardCount = 0;
  size_t servingShardCount = 0;
  string error;
};

struct ServingShardFile
{
  string inputPath;
  string outputName;
  uint64_t shardId = 0;
};

bool DiscoverServingInputShards(const string& inputDirectory, vector<ServingShardFile>* primary,
                                vector<string>* anchors, string* error);
bool WriteEmptyServingShard(const string& outputPath, uint64_t shardId, uint32_t blockSize,
                            string* error);
bool RebuildServingIndex(const ServingRebuildOptions& options, ServingRebuildStats* stats);
bool RebuildServingIndexSkeleton(const ServingRebuildOptions& options,
                                 ServingRebuildStats* stats);

} // namespace search
} // namespace avengine
