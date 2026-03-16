#include "index.h"

#include <avengine/crawler/url_normalizer.h>
#include <avengine/html_parser/HtmlParser.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace
{
struct IndexedDocument
{
  avengine::string url;
  avengine::string content;
};

struct TokenCounts
{
  size_t body = 0;
  size_t title = 0;
  size_t url = 0;

  void add(const TokenCounts& other)
  {
    body += other.body;
    title += other.title;
    url += other.url;
  }
};

static size_t tokenize_and_ingest(const avengine::string& input, PhraseKind kind)
{
  size_t ingested = 0;
  avengine::string token;

  for (size_t i = 0; i < input.size(); i++)
  {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (isalpha(c))
    {
      token.pushBack(static_cast<char>(tolower(c)));
      continue;
    }

    if (token.size() != 0)
    {
      globalIndex.ingest(token.cstr(), kind);
      ingested++;
      token = avengine::string();
    }
  }

  if (token.size() != 0)
  {
    globalIndex.ingest(token.cstr(), kind);
    ingested++;
  }

  return ingested;
}

static void parse_tab_file(char* content, size_t length,
                           avengine::vector<IndexedDocument>& documents)
{
  char* lineStart = content;
  for (size_t i = 0; i <= length; i++)
  {
    if (i != length && content[i] != '\n')
      continue;

    content[i] = '\0';
    if (*lineStart != '\0')
    {
      char* tab = strchr(lineStart, '\t');
      if (tab)
      {
        *tab = '\0';
        documents.pushBack(
            {.url = avengine::string(lineStart), .content = avengine::string(tab + 1)});
      }
    }

    lineStart = content + i + 1;
  }
}

static char* read_file(char const* filename, size_t& length)
{
  int fd = open(filename, O_RDONLY);
  if (fd == -1)
    return nullptr;

  struct stat sb = {};
  if (fstat(fd, &sb) != 0)
  {
    close(fd);
    return nullptr;
  }

  length = static_cast<size_t>(sb.st_size);
  char* data = static_cast<char*>(malloc(length + 1));
  if (!data)
  {
    close(fd);
    return nullptr;
  }

  size_t totalRead = 0;
  while (totalRead < length)
  {
    const ssize_t n = read(fd, data + totalRead, length - totalRead);
    if (n <= 0)
    {
      free(data);
      close(fd);
      return nullptr;
    }
    totalRead += static_cast<size_t>(n);
  }

  data[length] = '\0';
  close(fd);
  return data;
}

static bool is_directory(char const* path)
{
  if (!path)
    return false;

  struct stat s = {};
  if (stat(path, &s) != 0)
    return false;

  return S_ISDIR(s.st_mode);
}

static void list_files(const char* dir, avengine::vector<avengine::string>& files)
{
  DIR* dr = opendir(dir);
  if (!dr)
    return;

  struct dirent* en = nullptr;
  while ((en = readdir(dr)) != nullptr)
  {
    if (en->d_name[0] != '.')
      files.pushBack(avengine::string(en->d_name));
  }

  closedir(dr);
}

static TokenCounts index_html_document(avengine::crawler::URLNormalizer& normalizer,
                                       const avengine::string& url, const char* content,
                                       size_t length)
{
  avengine::string canonicalUrl = normalizer.normalizeIndividual(url);
  if (canonicalUrl.size() == 0)
    canonicalUrl = url;

  globalIndex.beginDocument(canonicalUrl.cstr());

  avengine::HtmlParser parser(content, length);

  TokenCounts counts = {};
  for (size_t i = 0; i < parser.words.size(); i++)
    counts.body += tokenize_and_ingest(parser.words[i], PhraseKind::BODY);

  for (size_t i = 0; i < parser.titleWords.size(); i++)
    counts.title += tokenize_and_ingest(parser.titleWords[i], PhraseKind::TITLE);

  counts.url += tokenize_and_ingest(canonicalUrl, PhraseKind::URL);
  return counts;
}

static void usage(const char* program)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s <directory>\n", program);
  fprintf(stderr, "  %s <verses.txt>\n", program);
}
} // namespace

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    usage(argv[0]);
    return 1;
  }

  avengine::crawler::URLNormalizer normalizer;
  TokenCounts total = {};

  const clock_t start = clock();
  const bool inputIsDirectory = is_directory(argv[1]);

  if (inputIsDirectory)
  {
    avengine::vector<avengine::string> files;
    list_files(argv[1], files);

    for (size_t i = 0; i < files.size(); i++)
    {
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "%s%s", argv[1], files[i].cstr());

      size_t length = 0;
      char* content = read_file(filepath, length);
      if (!content)
      {
        fprintf(stderr, "Failed to load file: %s\n", filepath);
        return 1;
      }

      total.add(index_html_document(normalizer, files[i], content, length));
      free(content);
    }
  }
  else
  {
    size_t length = 0;
    char* content = read_file(argv[1], length);
    if (!content)
    {
      fprintf(stderr, "Failed to load file: %s\n", argv[1]);
      return 1;
    }

    avengine::vector<IndexedDocument> documents;
    parse_tab_file(content, length, documents);

    for (size_t i = 0; i < documents.size(); i++)
    {
      total.add(index_html_document(normalizer, documents[i].url, documents[i].content.cstr(),
                                    documents[i].content.size()));
    }

    free(content);
  }

  clock_t diff = clock() - start;
  int msec = static_cast<int>(diff * 1000 / CLOCKS_PER_SEC);
  printf("Done building index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);
  printf("Indexed %zu body tokens, %zu title tokens, %zu url tokens\n", total.body, total.title,
         total.url);

  const clock_t writeStart = clock();
  int fd = open("index.sfi", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    perror("open");
    return 1;
  }

  if (globalIndex.serialize(fd))
  {
    close(fd);
    fprintf(stderr, "Failed to write index.sfi\n");
    return 1;
  }
  close(fd);

  diff = clock() - writeStart;
  msec = static_cast<int>(diff * 1000 / CLOCKS_PER_SEC);
  printf("Done writing index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);

  return 0;
}
