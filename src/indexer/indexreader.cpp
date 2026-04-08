#include "avengine/avengine_lib/string.h"
#include "avengine/codec.h"
#include "avengine/string.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct DictEntry
{
  avengine::string term;
  uint64_t postingOffset;
};

struct ParsedSkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

struct DocInfo
{
  uint64_t endPosition;
  avengine::string title;
  avengine::string url;
};

constexpr uint64_t kIndexMagic = 0x008FB8EF84979FF0ULL;

static size_t cstr_len(const char* s)
{
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}

static bool cstr_equal(const char* a, const char* b)
{
  while (*a && *b)
  {
    if (*a != *b)
      return false;
    a++;
    b++;
  }
  return *a == *b;
}

static void cstr_copy_lower(char* dst, const char* src, size_t dstCap)
{
  if (dstCap == 0)
    return;

  size_t i = 0;
  while (src[i] && i + 1 < dstCap)
  {
    dst[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(src[i])));
    i++;
  }
  dst[i] = '\0';
}

static bool posting_bounds_for_term(const avengine::vector<DictEntry>& terms, size_t termIndex,
                                    size_t postingsStart, size_t fileSize, size_t* outStart,
                                    size_t* outEnd)
{
  if (termIndex >= terms.size() || !outStart || !outEnd)
    return false;

  const uint64_t current = terms[termIndex].postingOffset;
  uint64_t next = static_cast<uint64_t>(fileSize - postingsStart);

  for (const DictEntry& entry : terms)
  {
    if (entry.postingOffset > current && entry.postingOffset < next)
      next = entry.postingOffset;
  }

  const size_t start = postingsStart + static_cast<size_t>(current);
  const size_t end = postingsStart + static_cast<size_t>(next);
  if (start + 24 > end || end > fileSize)
    return false;

  *outStart = start;
  *outEnd = end;
  return true;
}

static bool parse_docend_posting(const uint8_t* data, size_t fileSize, size_t postingsStart,
                                 const avengine::vector<DictEntry>& terms,
                                 avengine::vector<DocInfo>& docInfos)
{
  ssize_t docendIdx = -1;
  for (size_t i = 0; i < terms.size(); i++)
  {
    if (cstr_equal(terms[i].term.cstr(), "DOCUMENT_END"))
    {
      docendIdx = static_cast<ssize_t>(i);
      break;
    }
  }

  if (docendIdx < 0)
  {
    std::fprintf(stderr, "Warning: DOCUMENT_END not found in index; titles unavailable.\n");
    return false;
  }

  size_t startOff = 0;
  size_t endOff = 0;
  if (!posting_bounds_for_term(terms, static_cast<size_t>(docendIdx), postingsStart, fileSize,
                               &startOff, &endOff))
  {
    std::fprintf(stderr, "Warning: DOCUMENT_END posting list has corrupt boundaries.\n");
    return false;
  }

  avengine::Reader r(data + startOff, endOff - startOff);

  const uint64_t docFreq = r.readU64();
  const uint64_t collFreq = r.readU64();
  const uint64_t skipCount = r.readU64();

  for (uint64_t i = 0; i < skipCount; i++)
  {
    for (int field = 0; field < 3; field++)
      r.readVarU32();
  }

  avengine::vector<uint64_t> boundaryPositions;
  {
    uint64_t pos = 0;
    for (uint64_t i = 0; i < collFreq; i++)
    {
      const uint32_t delta = r.readVarU32();
      pos += delta;
      boundaryPositions.pushBack(pos);
    }
  }

  for (uint64_t i = 0; i < docFreq; i++)
  {
    const avengine::string url = r.readString();
    const avengine::string title = r.readString();
    r.skip(8);

    uint64_t endPos = (i < boundaryPositions.size()) ? boundaryPositions[i] : UINT64_MAX;
    docInfos.pushBack({endPos, title, url});
  }

  return true;
}

static size_t find_doc_for_position(const avengine::vector<DocInfo>& docInfos, uint64_t pos)
{
  for (size_t i = 0; i < docInfos.size(); i++)
  {
    if (docInfos[i].endPosition > pos)
      return i;
  }
  return docInfos.size();
}

static void usage(const char* program)
{
  std::fprintf(stderr, "Usage:\n");
  std::fprintf(stderr, "  %s <index.sfi> --list\n", program);
  std::fprintf(stderr, "  %s <index.sfi> <term>\n", program);
}

int main(int argc, char** argv)
{
  if (argc != 3)
  {
    usage(argv[0]);
    return 1;
  }

  const char* filename = argv[1];
  const bool listMode = cstr_equal(argv[2], "--list");

  int fd = open(filename, O_RDONLY);
  if (fd < 0)
  {
    std::perror("open");
    return 1;
  }

  struct stat sb;
  if (fstat(fd, &sb) != 0)
  {
    std::perror("fstat");
    close(fd);
    return 1;
  }

  if (sb.st_size < static_cast<off_t>(5 * sizeof(uint64_t)))
  {
    std::fprintf(stderr, "Index too small.\n");
    close(fd);
    return 1;
  }

  uint8_t* data = static_cast<uint8_t*>(mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (data == MAP_FAILED)
  {
    std::perror("mmap");
    return 1;
  }

  avengine::Reader fileHeaderReader(data, static_cast<size_t>(sb.st_size));
  const uint64_t magic = fileHeaderReader.readU64();
  const uint64_t documentCount = fileHeaderReader.readU64();
  const uint64_t uniqueWords = fileHeaderReader.readU64();
  const uint64_t tokenOffset = fileHeaderReader.readU64();
  const uint64_t rankingInputSize = fileHeaderReader.readU64();

  if (magic != kIndexMagic)
  {
    std::fprintf(stderr, "Corrupt index: bad magic.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  avengine::vector<DictEntry> terms;
  for (uint64_t i = 0; i < uniqueWords; i++)
  {
    const avengine::string term = fileHeaderReader.readString();
    (void)fileHeaderReader.readU64();
    (void)fileHeaderReader.readU64();
    const uint64_t postingOffset = fileHeaderReader.readU64();
    terms.pushBack({term, postingOffset});
  }

  const size_t postingsStart = static_cast<size_t>(fileHeaderReader.cursor() - data);
  if (postingsStart > static_cast<size_t>(sb.st_size))
  {
    std::fprintf(stderr, "Corrupt index: postings start exceeds file size.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  std::printf("Documents: %llu\n", static_cast<unsigned long long>(documentCount));
  std::printf("Unique words: %llu\n", static_cast<unsigned long long>(uniqueWords));
  std::printf("Token offset: %llu\n", static_cast<unsigned long long>(tokenOffset));
  std::printf("Ranking input size: %llu\n", static_cast<unsigned long long>(rankingInputSize));
  std::printf("Parsed terms: %zu\n", terms.size());

  if (listMode)
  {
    for (const DictEntry& entry : terms)
      std::printf("%s\n", entry.term.cstr());

    munmap(data, sb.st_size);
    return 0;
  }

  size_t rawLen = cstr_len(argv[2]);
  char* queryLower = static_cast<char*>(std::malloc(rawLen + 1));
  if (!queryLower)
  {
    std::fprintf(stderr, "Out of memory.\n");
    munmap(data, sb.st_size);
    return 1;
  }
  cstr_copy_lower(queryLower, argv[2], rawLen + 1);

  ssize_t foundIndex = -1;
  for (size_t i = 0; i < terms.size(); i++)
  {
    if (cstr_equal(terms[i].term.cstr(), queryLower))
    {
      foundIndex = static_cast<ssize_t>(i);
      break;
    }
  }

  if (foundIndex < 0)
  {
    std::printf("Term '%s' not found.\n", queryLower);
    std::free(queryLower);
    munmap(data, sb.st_size);
    return 0;
  }

  size_t startOff = 0;
  size_t endOff = 0;
  if (!posting_bounds_for_term(terms, static_cast<size_t>(foundIndex), postingsStart,
                               static_cast<size_t>(sb.st_size), &startOff, &endOff))
  {
    std::fprintf(stderr, "Corrupt posting list boundaries.\n");
    std::free(queryLower);
    munmap(data, sb.st_size);
    return 1;
  }

  avengine::Reader postReader(data + startOff, endOff - startOff);

  const uint64_t documentFrequency = postReader.readU64();
  const uint64_t collectionFrequency = postReader.readU64();
  const uint64_t skipCount = postReader.readU64();

  avengine::vector<ParsedSkipPointer> parsedSkips;
  for (uint64_t i = 0; i < skipCount; i++)
  {
    const uint32_t positionIndex = postReader.readVarU32();
    const uint32_t positionValue = postReader.readVarU32();
    const uint32_t byteOffset = postReader.readVarU32();
    parsedSkips.pushBack({positionIndex, positionValue, byteOffset});
  }

  avengine::vector<DocInfo> docInfos;
  parse_docend_posting(data, static_cast<size_t>(sb.st_size), postingsStart, terms, docInfos);

  std::printf("Term: %s\n", queryLower);
  std::printf("Document frequency: %llu\n", static_cast<unsigned long long>(documentFrequency));
  std::printf("Collection frequency: %llu\n", static_cast<unsigned long long>(collectionFrequency));
  std::printf("Skip pointers: %zu\n", parsedSkips.size());
  if (!parsedSkips.empty())
  {
    std::printf("Skip preview:");
    for (size_t i = 0; i < parsedSkips.size() && i < 8; i++)
    {
      const ParsedSkipPointer& sp = parsedSkips[i];
      std::printf(" [idx=%u pos=%u off=%u]", sp.positionIndex, sp.positionValue, sp.byteOffset);
    }
    std::printf("\n");
  }

  size_t decoded = 0;
  uint64_t pos64 = 0;

  std::printf("Positions:\n");
  while (!postReader.empty())
  {
    const uint32_t delta = postReader.readVarU32();
    pos64 += delta;

    const char* title = "(unknown)";
    const char* url = "(unknown)";

    size_t docIdx = find_doc_for_position(docInfos, pos64);
    if (docIdx < docInfos.size())
    {
      title = docInfos[docIdx].title.cstr();
      url = docInfos[docIdx].url.cstr();
    }

    std::printf("  %llu (%s) `%s`\n", static_cast<unsigned long long>(pos64), title, url);
    decoded++;
  }

  std::printf("Decoded positions: %zu\n", decoded);

  std::free(queryLower);
  munmap(data, sb.st_size);
  return 0;
}
