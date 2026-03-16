#include "index.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void parseTabFile(char* content, avengine::vector<char*>& documents,
                  avengine::vector<char*>& documentNames)
{
  char* verse = strtok(content, "\n");

  while (verse)
  {
    documentNames.pushBack(verse);

    while (*verse != '\t')
      verse++;

    *(verse++) = '\0';

    documents.pushBack(verse);

    verse = strtok(nullptr, "\n");
  }
}

char* memoryMapFile(char const* filename, size_t& length)
{
  int fd = open(filename, O_RDONLY);
  struct stat sb;
  if (fd == -1)
    return nullptr;

  if (fstat(fd, &sb) == -1)
    return nullptr;

  char* data =
      static_cast<char*>(mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));

  length = sb.st_size;

  // This might cause undefined behavior if it hits a new page.
  data[length] = '\0';
  close(fd);

  return data;
}

bool isDirectory(char const* path)
{
  struct stat s;

  if (stat(path, &s))
    return false;

  return s.st_mode & S_IFDIR;
}

void listFiles(char* dir, avengine::vector<char*>& files)
{
  DIR* dr;
  struct dirent* en;
  dr = opendir(dir);
  if (dr)
  {
    while ((en = readdir(dr)) != NULL)
    {
      if (en->d_name[0] != '.')
      {
        char* nameCopy = new char[strlen(en->d_name) + 1];
        strcpy(nameCopy, en->d_name);
        files.pushBack(nameCopy);
      }
    }
    closedir(dr);
  }
}

static void usage(const char* program)
{
  std::fprintf(stderr, "Usage:\n");
  std::fprintf(stderr, "  %s <directory>\n", program);
  std::fprintf(stderr, "  %s <verses.txt>\n", program);
}

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    usage(argv[0]);
    return 1;
  }

  clock_t start, diff;

  if (isDirectory(argv[1]))
  {
    avengine::vector<char*> files;

    listFiles(argv[1], files);

    start = clock();

    for (char* file : files)
    {
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "%s%s", argv[1], file);

      size_t length;
      char* content = memoryMapFile(filepath, length);
      assert(content);

      size_t written = indexDocument(file, content);

      munmap(content, length);
    }
  }
  else
  {
    size_t length;
    char* content = memoryMapFile(argv[1], length);

    printf("Done mapping file!\n");

    avengine::vector<char*> documents;
    avengine::vector<char*> documentNames;
    parseTabFile(content, documents, documentNames);

    printf("Done parsing verses file, %ld documents!\n", documents.size());

    start = clock();
    for (size_t i = 0; i < documents.size(); i++)
    {
      indexDocument(documentNames[i], documents[i]);
    }
  }

  diff = clock() - start;

  int msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("Done building index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);

  start = clock();

  int fd = open("index.sfi", O_WRONLY | O_CREAT, 0644);
  globalIndex.serialize(fd);
  close(fd);

  diff = clock() - start;

  msec = diff * 1000 / CLOCKS_PER_SEC;
  printf("Done writing index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);
}
