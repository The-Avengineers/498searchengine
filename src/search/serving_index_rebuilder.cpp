#include <avengine/search/serving_index_format.h>
#include <avengine/search/serving_index_rebuilder.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace avengine
{
namespace search
{
namespace
{

bool IsSeparator(char c)
{
  return c == '/';
}

string JoinPath(const string& directory, const string& filename)
{
  if (directory.size() == 0 || directory == string("."))
    return filename;

  string out(directory);
  if (out.size() > 0 && !IsSeparator(out[out.size() - 1]))
    out.pushBack('/');
  out += filename;
  return out;
}

void SetErrnoError(const char* prefix, const string& path, string* error)
{
  if (!error)
    return;
  *error = string(prefix);
  *error += string(": ");
  *error += path;
  *error += string(": ");
  *error += string(std::strerror(errno));
}

bool IsPrimaryShardName(const char* name, uint64_t* shardId)
{
  if (!name)
    return false;

  uint64_t id = 0;
  for (size_t i = 0; i < 5; ++i)
  {
    if (name[i] < '0' || name[i] > '9')
      return false;
    id = (id * 10u) + static_cast<uint64_t>(name[i] - '0');
  }

  if (name[5] != '.' || name[6] != 's' || name[7] != 'f' || name[8] != 'i' ||
      name[9] != '\0')
  {
    return false;
  }

  if (shardId)
    *shardId = id;
  return true;
}

bool IsAnchorShardName(const char* name)
{
  static const char kPrefix[] = "anchor_";
  if (!name)
    return false;

  size_t i = 0;
  for (; kPrefix[i] != '\0'; ++i)
  {
    if (name[i] != kPrefix[i])
      return false;
  }

  uint64_t ignored = 0;
  return IsPrimaryShardName(name + i, &ignored);
}

string ServingOutputName(uint64_t shardId)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "serving_%05llu.ssi",
                static_cast<unsigned long long>(shardId));
  return string(buf);
}

bool WriteAll(int fd, const void* data, size_t bytes)
{
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t written = 0;
  while (written < bytes)
  {
    const ssize_t n = write(fd, p + written, bytes - written);
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    written += static_cast<size_t>(n);
  }
  return true;
}

} // namespace

bool DiscoverServingInputShards(const string& inputDirectory, vector<ServingShardFile>* primary,
                                vector<string>* anchors, string* error)
{
  if (!primary || !anchors)
    return false;

  primary->clear();
  anchors->clear();

  DIR* dir = opendir(inputDirectory.cstr());
  if (!dir)
  {
    SetErrnoError("opendir", inputDirectory, error);
    return false;
  }

  for (dirent* ent = readdir(dir); ent != nullptr; ent = readdir(dir))
  {
    uint64_t shardId = 0;
    if (IsPrimaryShardName(ent->d_name, &shardId))
    {
      const string name(ent->d_name);
      primary->pushBack({JoinPath(inputDirectory, name), ServingOutputName(shardId), shardId});
    }
    else if (IsAnchorShardName(ent->d_name))
    {
      anchors->pushBack(JoinPath(inputDirectory, string(ent->d_name)));
    }
  }

  closedir(dir);

  primary->sort([](const ServingShardFile& a, const ServingShardFile& b) {
    if (a.shardId != b.shardId)
      return a.shardId < b.shardId;
    return a.inputPath < b.inputPath;
  });
  anchors->sort([](const string& a, const string& b) {
    return a < b;
  });
  return true;
}

bool WriteEmptyServingShard(const string& outputPath, uint64_t shardId, uint32_t blockSize,
                            string* error)
{
  if (blockSize == 0)
  {
    if (error)
      *error = string("block size must be greater than zero");
    return false;
  }

  ServingShardHeaderV1 header = MakeEmptyServingShardHeaderV1(shardId);
  header.blockSize = blockSize;

  const int fd = open(outputPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    SetErrnoError("open", outputPath, error);
    return false;
  }

  bool ok = WriteAll(fd, &header, sizeof(header));
  if (close(fd) != 0)
    ok = false;

  if (!ok)
  {
    SetErrnoError("write", outputPath, error);
    unlink(outputPath.cstr());
    return false;
  }

  return true;
}

bool RebuildServingIndexSkeleton(const ServingRebuildOptions& options, ServingRebuildStats* stats)
{
  ServingRebuildStats local = {};
  if (!stats)
    stats = &local;
  *stats = {};

  if (options.inputDirectory.size() == 0)
  {
    stats->error = string("missing input directory");
    return false;
  }
  if (options.outputDirectory.size() == 0)
  {
    stats->error = string("missing output directory");
    return false;
  }
  if (options.blockSize == 0)
  {
    stats->error = string("block size must be greater than zero");
    return false;
  }

  vector<ServingShardFile> primary;
  vector<string> anchors;
  if (!DiscoverServingInputShards(options.inputDirectory, &primary, &anchors, &stats->error))
    return false;

  stats->primaryShardCount = primary.size();
  stats->anchorShardCount = anchors.size();

  if (primary.empty())
  {
    stats->error = string("no primary NNNNN.sfi shards found in ");
    stats->error += options.inputDirectory;
    return false;
  }

  struct stat outStat = {};
  if (stat(options.outputDirectory.cstr(), &outStat) != 0)
  {
    SetErrnoError("stat", options.outputDirectory, &stats->error);
    return false;
  }
  if (!S_ISDIR(outStat.st_mode))
  {
    stats->error = string("output path is not a directory: ");
    stats->error += options.outputDirectory;
    return false;
  }

  for (size_t i = 0; i < primary.size(); ++i)
  {
    const string outputPath = JoinPath(options.outputDirectory, primary[i].outputName);
    if (!WriteEmptyServingShard(outputPath, primary[i].shardId, options.blockSize, &stats->error))
      return false;
    ++stats->servingShardCount;
  }

  return true;
}

} // namespace search
} // namespace avengine
