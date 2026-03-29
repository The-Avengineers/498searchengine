#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/string_slice.h"
#include "avengine/avengine_lib/vector.h"
#include <avengine/indexer/indexer.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Parses a tab-delimited file where each line has the form:
//   <name>\t<document text>
// Appends non-owning slices into the same underlying buffer to `documents`
// and `documentNames`.  The buffer must remain alive for the lifetime of
// every slice stored in the output vectors.
void parseTabFile(avengine::string_slice content,
                  avengine::vector<avengine::string_slice>& documents,
                  avengine::vector<avengine::string_slice>& documentNames)
{
  avengine::vector<avengine::string_slice> lines = content.split('\n');

  for (size_t i = 0; i < lines.size(); ++i)
  {
    avengine::string_slice line = lines[i];
    if (line.empty())
      continue;

    size_t tabPos = line.find("\t");
    if (tabPos == avengine::string_slice::npos)
      continue;

    documentNames.pushBack(line.substr(0, tabPos));
    documents.pushBack(line.substr(tabPos + 1, line.size() - tabPos - 1));
  }
}

// Memory-maps `filename` for reading (copy-on-write) and returns a
// string_slice covering the mapped region.  Returns an empty slice on error.
// The caller must munmap(slice.begin(), slice.size()) when done.
avengine::string_slice memoryMapFile(avengine::string_slice filename)
{
  avengine::string filenameStr = filename.toString();
  int fd = open(filenameStr.cstr(), O_RDONLY);
  if (fd == -1)
    return avengine::string_slice();

  struct stat sb;
  if (fstat(fd, &sb) == -1)
    return avengine::string_slice();

  size_t length = static_cast<size_t>(sb.st_size);
  char* data =
      static_cast<char*>(mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));

  close(fd);

  return avengine::string_slice(data, length);
}

bool isDirectory(avengine::string_slice path)
{
  struct stat s;
  avengine::string pathStr = path.toString();

  if (stat(pathStr.cstr(), &s))
    return false;

  return s.st_mode & S_IFDIR;
}

void listFiles(avengine::string_slice dir, avengine::vector<avengine::string>& files)
{
  avengine::string dirStr = dir.toString();
  DIR* dr = opendir(dirStr.cstr());
  if (dr)
  {
    struct dirent* en;
    while ((en = readdir(dr)) != NULL)
    {
      if (en->d_name[0] != '.')
        files.pushBack(avengine::string(en->d_name));
    }
    closedir(dr);
  }
}

static void usage(avengine::string_slice program)
{
  std::fprintf(stderr, "Usage:\n");
  std::fprintf(stderr, "  %.*s <directory>\n", static_cast<int>(program.size()), program.begin());
  std::fprintf(stderr, "  %.*s <verses.txt>\n", static_cast<int>(program.size()), program.begin());
}

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    usage(argv[0]);
    return 1;
  }

  clock_t start, diff;

  avengine::string_slice arg1(argv[1]);

  if (isDirectory(arg1))
  {
    avengine::vector<avengine::string> files;

    listFiles(arg1, files);

    start = clock();
    auto phrases = avengine::vector<avengine::string_slice>();

    size_t totalFiles = files.size();
    double lastPrintedPercent = -1.0;

    for (size_t fileIdx = 0; fileIdx < totalFiles; ++fileIdx)
    {
      avengine::string& file = files[fileIdx];
      avengine::string filepath(argv[1]);
      filepath += file;

      avengine::string_slice content = memoryMapFile(avengine::string_slice(filepath));
      assert(content.begin());

      phrases.pushBack(content);

      auto metadata = DocumentMetadata{
          .url = file,
          .title = content.split('\n').front().toString(),
          .input = 0,
      };

      indexDocument(metadata, phrases);
      phrases.clear();

      munmap(content.begin(), content.size());

      double percent = (static_cast<double>(fileIdx + 1) / static_cast<double>(totalFiles)) * 100.0;
      double truncated = static_cast<int>(percent * 100.0) / 100.0;
      if (truncated != lastPrintedPercent)
      {
        lastPrintedPercent = truncated;
        std::printf("\rIndexing: %06.2f%%", percent);
        std::fflush(stdout);
      }
    }
    std::printf("\n");
  }
  else
  {
    avengine::string_slice content = memoryMapFile(arg1);

    printf("Done mapping file!\n");

    avengine::vector<avengine::string_slice> documents;
    avengine::vector<avengine::string_slice> documentNames;
    parseTabFile(content, documents, documentNames);

    printf("Done parsing verses file, %ld documents!\n", documents.size());

    start = clock();
    for (size_t i = 0; i < documents.size(); i++)
    {
      DocumentMetadata metadata = {
          .url = "(none)",
          .title = documentNames[i].toString(),
          .input = 0,
      };
      avengine::vector<avengine::string_slice> phrases;
      phrases.pushBack(documents[i]);
      indexDocument(metadata, phrases);
    }
  }

  diff = clock() - start;

  int msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("Done building index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);

  start = clock();

  cleanUpIndexer();

  diff = clock() - start;

  msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("Done writing index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);
}
