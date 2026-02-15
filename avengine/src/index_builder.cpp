#include "index.h"
#include <avengine/HtmlParser.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
struct PendingLink
{
  uint32_t sourceDocId;
  std::string sourceUrl;
  std::string baseUrl;
  std::string href;
  std::vector<std::string> anchorText;
};

struct NormalizedUrl
{
  std::string protocol;
  std::string host;
  std::string port;
  std::string path;
};

static std::string lower_ascii(std::string value)
{
  for (char& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

static std::string resolve_dot_segments(const std::string& path)
{
  if (path.empty())
    return "/";

  const bool absolute = path[0] == '/';
  std::vector<std::string> segments;
  std::string current;

  for (size_t i = 0; i <= path.size(); i++)
  {
    if (i < path.size() && path[i] != '/')
    {
      current.push_back(path[i]);
      continue;
    }

    segments.push_back(current);
    current.clear();
  }

  std::vector<std::string> resolved;
  for (const std::string& seg : segments)
  {
    if (seg.empty() || seg == ".")
      continue;
    if (seg == "..")
    {
      if (!resolved.empty())
        resolved.pop_back();
      continue;
    }
    resolved.push_back(seg);
  }

  std::string output = absolute ? "/" : "";
  for (size_t i = 0; i < resolved.size(); i++)
  {
    if (i > 0)
      output.push_back('/');
    output += resolved[i];
  }

  if (output.empty())
    return absolute ? "/" : "";

  if (absolute && output[0] != '/')
    output.insert(output.begin(), '/');

  return output;
}

static std::string build_url(const NormalizedUrl& url)
{
  std::string out = url.protocol + "://" + url.host;
  if (!url.port.empty())
    out += ":" + url.port;
  out += url.path;
  return out;
}

static NormalizedUrl parse_normalized_url(const std::string& rawUrl)
{
  std::string input = rawUrl;
  const size_t hashPos = input.find('#');
  if (hashPos != std::string::npos)
    input.resize(hashPos);

  NormalizedUrl url = {};

  size_t hostStart = 0;
  const size_t protoPos = input.find("://");
  if (protoPos != std::string::npos)
  {
    url.protocol = lower_ascii(input.substr(0, protoPos));
    hostStart = protoPos + 3;
  }
  else
  {
    url.protocol = "http";
  }

  const size_t pathPos = input.find('/', hostStart);
  const std::string hostPort = lower_ascii(input.substr(hostStart, pathPos - hostStart));

  const size_t portPos = hostPort.find(':');
  if (portPos == std::string::npos)
  {
    url.host = hostPort;
    url.port.clear();
  }
  else
  {
    url.host = hostPort.substr(0, portPos);
    url.port = hostPort.substr(portPos + 1);
    if ((url.protocol == "http" && url.port == "80") ||
        (url.protocol == "https" && url.port == "443"))
      url.port.clear();
  }

  if (pathPos == std::string::npos)
    url.path = "/";
  else
    url.path = input.substr(pathPos);

  url.path = resolve_dot_segments(url.path);
  if (url.path.empty())
    url.path = "/";

  if (url.path.size() > 1 && url.path.back() == '/')
    url.path.pop_back();

  return url;
}

static std::string normalize_individual_url(const std::string& url)
{
  if (url.empty())
    return "";
  return build_url(parse_normalized_url(url));
}

static std::string normalize_with_base_url(const std::string& base, const std::string& extension)
{
  if (extension.empty())
    return normalize_individual_url(base);

  if (extension.find("://") != std::string::npos)
    return normalize_individual_url(extension);

  NormalizedUrl baseUrl = parse_normalized_url(base);

  if (extension.size() >= 2 && extension[0] == '/' && extension[1] == '/')
  {
    return normalize_individual_url(baseUrl.protocol + ":" + extension);
  }

  if (!extension.empty() && extension[0] == '/')
  {
    baseUrl.path = resolve_dot_segments(extension);
    return build_url(baseUrl);
  }

  std::string basePath = baseUrl.path;
  const size_t lastSlash = basePath.rfind('/');
  if (lastSlash == std::string::npos)
    basePath = "/";
  else
    basePath = basePath.substr(0, lastSlash + 1);

  baseUrl.path = resolve_dot_segments(basePath + extension);
  if (baseUrl.path.empty())
    baseUrl.path = "/";
  return build_url(baseUrl);
}

static size_t tokenize_and_ingest_current(const std::string& input, PhraseKind kind)
{
  size_t ingested = 0;
  std::string token;

  for (char c : input)
  {
    if (std::isalpha(static_cast<unsigned char>(c)))
    {
      token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      continue;
    }

    if (!token.empty())
    {
      globalIndex.ingest(token.c_str(), kind);
      ingested++;
      token.clear();
    }
  }

  if (!token.empty())
  {
    globalIndex.ingest(token.c_str(), kind);
    ingested++;
  }

  return ingested;
}

static size_t
tokenize_and_ingest_into_document(uint32_t docId, const std::string& input, PhraseKind kind,
                                  std::unordered_set<std::string>* dedupTokens = nullptr)
{
  size_t ingested = 0;
  std::string token;

  auto flush_token = [&]()
  {
    if (token.empty())
      return;

    if (dedupTokens && !dedupTokens->insert(token).second)
    {
      token.clear();
      return;
    }

    globalIndex.ingestIntoDocument(docId, token.c_str(), kind);
    ingested++;
    token.clear();
  };

  for (char c : input)
  {
    if (std::isalpha(static_cast<unsigned char>(c)))
    {
      token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
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
                         std::vector<std::pair<std::string, std::string>>& documents)
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
        documents.emplace_back(std::string(lineStart), std::string(tab + 1));
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

static void listFiles(const char* dir, std::vector<std::string>& files)
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
indexDocumentWithHtmlAndCollectAnchors(const std::string& url, const char* content, size_t length,
                                       std::unordered_map<std::string, uint32_t>& urlToDocId,
                                       std::vector<PendingLink>& pendingLinks)
{
  const uint32_t docId = globalIndex.beginDocument(url.c_str());

  std::string canonicalUrl = normalize_individual_url(url);
  if (canonicalUrl.empty())
    canonicalUrl = url;

  urlToDocId[canonicalUrl] = docId;
  urlToDocId[url] = docId;

  avengine::HtmlParser parser(content, length);

  size_t tokens = 0;
  for (const std::string& word : parser.words)
    tokens += tokenize_and_ingest_current(word, PhraseKind::NORMAL);

  for (const std::string& word : parser.titleWords)
    tokens += tokenize_and_ingest_current(word, PhraseKind::TITLE);

  std::string baseForResolve = canonicalUrl;
  if (!parser.base.empty())
    baseForResolve = normalize_with_base_url(canonicalUrl, parser.base);

  for (const avengine::Link& link : parser.links)
  {
    PendingLink pending;
    pending.sourceDocId = docId;
    pending.sourceUrl = canonicalUrl;
    pending.baseUrl = baseForResolve;
    pending.href = link.URL;
    pending.anchorText.assign(link.anchorText.begin(), link.anchorText.end());
    pendingLinks.push_back(std::move(pending));
  }

  return tokens;
}

static size_t indexAnchorTextToTargets(const std::unordered_map<std::string, uint32_t>& urlToDocId,
                                       const std::vector<PendingLink>& pendingLinks)
{
  size_t anchorTokens = 0;

  for (const PendingLink& pending : pendingLinks)
  {
    std::string targetUrl = normalize_with_base_url(pending.baseUrl, pending.href);
    if (targetUrl.empty())
      continue;

    auto targetIt = urlToDocId.find(targetUrl);
    if (targetIt == urlToDocId.end())
    {
      std::string canonicalTarget = normalize_individual_url(targetUrl);
      targetIt = urlToDocId.find(canonicalTarget);
      if (targetIt == urlToDocId.end())
        continue;
    }

    std::unordered_set<std::string> dedupTokens;
    for (const std::string& word : pending.anchorText)
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

  std::unordered_map<std::string, uint32_t> urlToDocId;
  std::vector<PendingLink> pendingLinks;

  clock_t start = clock();
  size_t bodyAndTitleTokens = 0;

  const bool inputIsDirectory = isDirectory(argv[1]);

  if (inputIsDirectory)
  {
    std::vector<std::string> files;
    listFiles(argv[1], files);

    for (const std::string& file : files)
    {
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "%s%s", argv[1], file.c_str());

      size_t length = 0;
      char* content = memoryMapFile(filepath, length);
      if (!content)
      {
        std::fprintf(stderr, "Failed to load file: %s\n", filepath);
        return 1;
      }

      bodyAndTitleTokens +=
          indexDocumentWithHtmlAndCollectAnchors(file, content, length, urlToDocId, pendingLinks);

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

    std::vector<std::pair<std::string, std::string>> documents;
    parseTabFile(content, length, documents);

    for (const auto& doc : documents)
    {
      bodyAndTitleTokens += indexDocumentWithHtmlAndCollectAnchors(
          doc.first, doc.second.c_str(), doc.second.size(), urlToDocId, pendingLinks);
    }

    std::free(content);
  }

  size_t anchorTokens = indexAnchorTextToTargets(urlToDocId, pendingLinks);

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
