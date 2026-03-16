#include "index.h"
#include <avengine/crawler/url_normalizer.h>
#include <avengine/html_parser/HtmlParser.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
struct AvStringHash
{
  size_t operator()(const avengine::string& value) const
  {
    return djb2Hash(value.cstr());
  }
};

using StringToDocIdMap = std::unordered_map<avengine::string, uint32_t, AvStringHash>;
using StringSet = std::unordered_set<avengine::string, AvStringHash>;

struct PendingLink
{
  uint32_t sourceDocId;
  avengine::string sourceUrl;
  avengine::string baseUrl;
  avengine::string href;
  avengine::vector<avengine::string> anchorText;
};

static size_t tokenize_and_ingest_current(const avengine::string& input, PhraseKind kind)
{
  size_t ingested = 0;
  avengine::string token;

  for (char c : input)
  {
    if (std::isalpha(static_cast<unsigned char>(c)))
    {
      token.pushBack(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
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

static size_t
tokenize_and_ingest_into_document(uint32_t docId, const avengine::string& input, PhraseKind kind,
                                  StringSet* dedupTokens = nullptr)
{
  size_t ingested = 0;
  avengine::string token;

  auto flush_token = [&]()
  {
    if (token.size() == 0)
      return;

    if (dedupTokens && !dedupTokens->insert(token).second)
    {
      token = avengine::string();
      return;
    }

    globalIndex.ingestIntoDocument(docId, token.cstr(), kind);
    ingested++;
    token = avengine::string();
  };

  for (char c : input)
  {
    if (std::isalpha(static_cast<unsigned char>(c)))
    {
      token.pushBack(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    else
    {
      flush_token();
    }
  }
  flush_token();

  return ingested;
}

static void parseTabFile(char* content, size_t length,
                         std::vector<std::pair<avengine::string, avengine::string>>& documents)
{
  char* lineStart = content;
  for (size_t i = 0; i <= length; i++)
  {
    if (i != length && content[i] != '\n')
      continue;

    content[i] = '\0';
    if (*lineStart)
    {
      char* tab = std::strchr(lineStart, '\t');
      if (tab)
      {
        *tab = '\0';
        documents.emplace_back(avengine::string(lineStart), avengine::string(tab + 1));
      }
    }

    lineStart = content + i + 1;
  }
}

static char* memoryMapFile(char const* filename, size_t& length)
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

  char* data = static_cast<char*>(std::malloc(length + 1));
  if (!data)
  {
    close(fd);
    return nullptr;
  }

  size_t totalRead = 0;
  while (totalRead < length)
  {
    ssize_t n = read(fd, data + totalRead, length - totalRead);
    if (n <= 0)
    {
      std::free(data);
      close(fd);
      return nullptr;
    }
    totalRead += static_cast<size_t>(n);
  }

  data[length] = '\0';
  close(fd);
  return data;
}

static bool isDirectory(char const* path)
{
  if (!path)
    return false;

  struct stat s = {};
  if (stat(path, &s) != 0)
    return false;

  return S_ISDIR(s.st_mode);
}

static void listFiles(const char* dir, std::vector<avengine::string>& files)
{
  DIR* dr;
  struct dirent* en;
  dr = opendir(dir);
  if (dr)
  {
    while ((en = readdir(dr)) != nullptr)
    {
      if (en->d_name[0] != '.')
        files.emplace_back(en->d_name);
    }
    closedir(dr);
  }
}

static size_t
indexDocumentWithHtmlAndCollectAnchors(avengine::crawler::URLNormalizer& normalizer,
                                       const avengine::string& url, const char* content,
                                       size_t length, StringToDocIdMap& urlToDocId,
                                       std::vector<PendingLink>& pendingLinks)
{
  const uint32_t docId = globalIndex.beginDocument(url.cstr());

  avengine::string canonicalUrl = normalizer.normalizeIndividual(url);
  if (canonicalUrl.size() == 0)
    canonicalUrl = url;

  urlToDocId[canonicalUrl] = docId;
  urlToDocId[url] = docId;

  avengine::HtmlParser parser(content, length);

  size_t tokens = 0;
  for (const avengine::string& word : parser.words)
    tokens += tokenize_and_ingest_current(word, PhraseKind::NORMAL);

  for (const avengine::string& word : parser.titleWords)
    tokens += tokenize_and_ingest_current(word, PhraseKind::TITLE);

  avengine::string baseForResolve = canonicalUrl;
  if (parser.base.size() != 0)
  {
    baseForResolve = normalizer.normalize(canonicalUrl, parser.base);
    if (baseForResolve.size() == 0)
      baseForResolve = canonicalUrl;
  }

  for (const avengine::Link& link : parser.links)
  {
    PendingLink pending;
    pending.sourceDocId = docId;
    pending.sourceUrl = canonicalUrl;
    pending.baseUrl = baseForResolve;
    pending.href = link.URL;
    for (const avengine::string& word : link.anchorText)
      pending.anchorText.pushBack(word);
    pendingLinks.push_back(std::move(pending));
  }

  return tokens;
}

static size_t indexAnchorTextToTargets(avengine::crawler::URLNormalizer& normalizer,
                                       const StringToDocIdMap& urlToDocId,
                                       const std::vector<PendingLink>& pendingLinks)
{
  size_t anchorTokens = 0;

  for (const PendingLink& pending : pendingLinks)
  {
    avengine::string targetUrl = normalizer.normalize(pending.baseUrl, pending.href);
    if (targetUrl.size() == 0)
      continue;

    auto targetIt = urlToDocId.find(targetUrl);
    if (targetIt == urlToDocId.end())
    {
      avengine::string canonicalTarget = normalizer.normalizeIndividual(targetUrl);
      targetIt = urlToDocId.find(canonicalTarget);
      if (targetIt == urlToDocId.end())
        continue;
    }

    StringSet dedupTokens;
    for (const avengine::string& word : pending.anchorText)
    {
      anchorTokens += tokenize_and_ingest_into_document(targetIt->second, word, PhraseKind::ANCHOR,
                                                        &dedupTokens);
    }
  }

  return anchorTokens;
}

static void usage(const char* program)
{
  std::fprintf(stderr, "Usage:\n");
  std::fprintf(stderr, "  %s <directory>\n", program);
  std::fprintf(stderr, "  %s <verses.txt>\n", program);
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
  StringToDocIdMap urlToDocId;
  std::vector<PendingLink> pendingLinks;

  clock_t start = clock();
  size_t bodyAndTitleTokens = 0;

  const bool inputIsDirectory = isDirectory(argv[1]);

  if (inputIsDirectory)
  {
    std::vector<avengine::string> files;
    listFiles(argv[1], files);

    for (const avengine::string& file : files)
    {
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "%s%s", argv[1], file.cstr());

      size_t length = 0;
      char* content = memoryMapFile(filepath, length);
      if (!content)
      {
        std::fprintf(stderr, "Failed to load file: %s\n", filepath);
        return 1;
      }

      bodyAndTitleTokens +=
          indexDocumentWithHtmlAndCollectAnchors(normalizer, file, content, length, urlToDocId,
                                                 pendingLinks);

      std::free(content);
    }
  }
  else
  {
    size_t length = 0;
    char* content = memoryMapFile(argv[1], length);
    if (!content)
    {
      std::fprintf(stderr, "Failed to load file: %s\n", argv[1]);
      return 1;
    }

    std::vector<std::pair<avengine::string, avengine::string>> documents;
    parseTabFile(content, length, documents);

    for (const auto& doc : documents)
    {
      bodyAndTitleTokens += indexDocumentWithHtmlAndCollectAnchors(
          normalizer, doc.first, doc.second.cstr(), doc.second.size(), urlToDocId, pendingLinks);
    }

    std::free(content);
  }

  size_t anchorTokens = indexAnchorTextToTargets(normalizer, urlToDocId, pendingLinks);

  clock_t diff = clock() - start;
  int msec = static_cast<int>(diff * 1000 / CLOCKS_PER_SEC);
  printf("Done building index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);
  printf("Indexed %zu body/title tokens and %zu anchor tokens\n", bodyAndTitleTokens, anchorTokens);

  start = clock();

  int fd = open("index.sfi", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    std::perror("open");
    return 1;
  }
  globalIndex.serialize(fd);
  close(fd);

  diff = clock() - start;
  msec = static_cast<int>(diff * 1000 / CLOCKS_PER_SEC);
  printf("Done writing index in %d seconds %d milliseconds\n", msec / 1000, msec % 1000);

  return 0;
}
