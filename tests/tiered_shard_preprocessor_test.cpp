#include <avengine/indexer/indexer.h>
#include <avengine/search/index_shard_source.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{

void Fail(int line, const char* expr)
{
  std::fprintf(stderr, "tiered_shard_preprocessor_test failed at line %d: %s\n", line, expr);
  std::exit(1);
}

#define EXPECT(expr)                                                                            \
  do                                                                                            \
  {                                                                                             \
    if (!(expr))                                                                                \
      Fail(__LINE__, #expr);                                                                    \
  } while (0)

std::string Join(const std::string& dir, const std::string& name)
{
  if (dir.empty() || dir.back() == '/')
    return dir + name;
  return dir + "/" + name;
}

std::string ShellQuote(const std::string& value)
{
  std::string out = "'";
  for (char c : value)
  {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

avengine::string Av(const char* text)
{
  return avengine::string(text);
}

void WriteShard(const std::string& path, const std::vector<const char*>& urls)
{
  Index index;
  for (size_t i = 0; i < urls.size(); ++i)
  {
    RankingInput input = {};
    input.domainDepth = static_cast<uint32_t>(i + 1);
    input.depth = static_cast<uint32_t>(i + 2);
    input.urlLength = static_cast<uint16_t>(std::strlen(urls[i]));
    input.titleWordCount = 2;
    input.seedSource = 1;

    DocumentMetadata metadata;
    metadata.url = Av(urls[i]);
    metadata.title = Av("Fixture Title");
    metadata.description = Av("Fixture description");
    metadata.input = input;

    index.beginDocument(metadata);
    index.ingest("alpha");
    index.ingest(i % 2 == 0 ? "beta" : "gamma");
    index.endDocument();
  }

  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  EXPECT(fd >= 0);
  EXPECT(index.serialize(fd) == false);
  EXPECT(close(fd) == 0);
}

void WriteBadShard(const std::string& path)
{
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  EXPECT(fd >= 0);
  const char bytes[] = "not an sfi shard";
  EXPECT(write(fd, bytes, sizeof(bytes)) == static_cast<ssize_t>(sizeof(bytes)));
  EXPECT(close(fd) == 0);
}

size_t CountSfiFiles(const std::string& dir)
{
  size_t count = 0;
  DIR* d = opendir(dir.c_str());
  EXPECT(d != nullptr);
  while (dirent* ent = readdir(d))
  {
    const std::string name(ent->d_name);
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".sfi")
      ++count;
  }
  closedir(d);
  return count;
}

void CollectSfiFiles(const std::string& dir, std::vector<std::string>& out)
{
  DIR* d = opendir(dir.c_str());
  EXPECT(d != nullptr);
  while (dirent* ent = readdir(d))
  {
    const std::string name(ent->d_name);
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".sfi")
      out.push_back(Join(dir, name));
  }
  closedir(d);
}

bool FileContains(const std::string& path, const char* needle)
{
  FILE* fp = fopen(path.c_str(), "r");
  EXPECT(fp != nullptr);
  char line[4096];
  while (fgets(line, sizeof(line), fp))
  {
    if (std::strstr(line, needle))
    {
      fclose(fp);
      return true;
    }
  }
  fclose(fp);
  return false;
}

void RunPreprocessor(const std::string& binary, const std::string& input, const std::string& output,
                     const std::string& checkpoint)
{
  const std::string command = ShellQuote(binary) + " --input " + ShellQuote(input) +
                              " --output " + ShellQuote(output) + " --checkpoint " +
                              ShellQuote(checkpoint) + " --threads 1 --force";
  const int rc = std::system(command.c_str());
  EXPECT(rc == 0);
}

} // namespace

int main(int argc, char** argv)
{
  EXPECT(argc >= 2);
  const std::string binary = argv[1];

  char rootTemplate[] = "/tmp/tiered_preprocessor_test_XXXXXX";
  char* rootRaw = mkdtemp(rootTemplate);
  EXPECT(rootRaw != nullptr);
  const std::string root(rootRaw);
  const std::string input = Join(root, "input");
  const std::string output = Join(root, "output");
  const std::string checkpoint = Join(output, ".tiered_preprocess.checkpoint");
  EXPECT(mkdir(input.c_str(), 0755) == 0);
  EXPECT(mkdir(output.c_str(), 0755) == 0);

  WriteShard(Join(input, "00000.sfi"), {"https://example.com/a", "https://school.edu/b"});
  WriteShard(Join(input, "anchor_00000.sfi"), {"https://example.com/a"});
  WriteBadShard(Join(input, "bad_00000.sfi"));

  RunPreprocessor(binary, input, output, checkpoint);
  const size_t firstCount = CountSfiFiles(output);
  EXPECT(firstCount > 0);
  EXPECT(FileContains(checkpoint, "FAIL"));

  std::vector<std::string> outputs;
  CollectSfiFiles(output, outputs);
  size_t loaded = 0;
  for (const std::string& path : outputs)
  {
    avengine::search::IndexShardSource shard(avengine::string(path.c_str()));
    if (shard.IsLoaded())
      ++loaded;
  }
  EXPECT(loaded == outputs.size());

  RunPreprocessor(binary, input, output, checkpoint);
  EXPECT(CountSfiFiles(output) == firstCount);
  EXPECT(FileContains(checkpoint, "DONE"));
  EXPECT(FileContains(checkpoint, "FAIL"));

  return 0;
}
