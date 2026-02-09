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
  uint64_t stringOffset;
  uint64_t postingOffset;
};

static uint64_t read_u64_le(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; i++)
    value |= static_cast<uint64_t>(ptr[i]) << (8 * i);
  return value;
}

static uint32_t read_var_u32(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  uint32_t result = 0;
  uint32_t shift = 0;

  while (begin < end && shift < 35)
  {
    uint8_t byte = *begin++;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u)
    {
      *next = begin;
      return result;
    }
    shift += 7;
  }

  *next = nullptr;
  return 0;
}

static size_t cstr_len_safe(const uint8_t* start, const uint8_t* end)
{
  const uint8_t* p = start;
  while (p < end && *p)
    p++;
  if (p == end)
    return static_cast<size_t>(-1);
  return static_cast<size_t>(p - start);
}

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

  if (sb.st_size < 16)
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

  // Layout starts with two u64 values:
  // [document count][unique words + 1]
  const uint64_t documentCount = read_u64_le(data);
  const uint64_t uniqueWordsPlusOne = read_u64_le(data + 8);
  const uint64_t tableSlots = uniqueWordsPlusOne * 2;
  const uint64_t dictionaryBytes = 16 + tableSlots * 16;

  if (dictionaryBytes > static_cast<uint64_t>(sb.st_size))
  {
    std::fprintf(stderr, "Corrupt index: dictionary exceeds file size.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  // Dictionary is a flat hash table where each slot is:
  // [string offset u64][posting offset u64]
  avengine::vector<DictEntry> entries;
  for (uint64_t i = 0; i < tableSlots; i++)
  {
    size_t offset = static_cast<size_t>(16 + i * 16);
    entries.pushBack({read_u64_le(data + offset), read_u64_le(data + offset + 8)});
  }

  const uint8_t* stringStart = data + dictionaryBytes;
  const uint8_t* fileEnd = data + sb.st_size;

  size_t inferredStringBytes = 0;
  size_t parsedTerms = 0;
  avengine::vector<DictEntry> usedEntries;
  avengine::vector<avengine::string> terms;

  // Scan all non-empty dictionary slots, recover C strings from string buffer,
  // and infer where postings begin by taking max(term_end_offset).
  for (const DictEntry& e : entries)
  {
    if (e.stringOffset == 0 && e.postingOffset == 0)
      continue;
    if (dictionaryBytes + e.stringOffset >= static_cast<uint64_t>(sb.st_size))
      continue;

    const uint8_t* termPtr = stringStart + e.stringOffset;
    size_t len = cstr_len_safe(termPtr, fileEnd);
    if (len == static_cast<size_t>(-1))
      continue;

    size_t endOff = static_cast<size_t>(e.stringOffset + len + 1);
    if (endOff > inferredStringBytes)
      inferredStringBytes = endOff;

    usedEntries.pushBack(e);
    terms.pushBack(avengine::string(reinterpret_cast<const char*>(termPtr)));
    parsedTerms++;
  }

  const size_t postingsStart = static_cast<size_t>(dictionaryBytes) + inferredStringBytes;
  if (postingsStart > static_cast<size_t>(sb.st_size))
  {
    std::fprintf(stderr, "Corrupt index: postings start exceeds file size.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  std::printf("Documents: %llu\n", static_cast<unsigned long long>(documentCount));
  std::printf("Dictionary slots: %llu\n", static_cast<unsigned long long>(tableSlots));
  std::printf("Parsed terms (best effort): %zu\n", parsedTerms);

  if (listMode)
  {
    for (const avengine::string& term : terms)
      std::printf("%s\n", term.cstr());

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
    if (cstr_equal(terms[i].cstr(), queryLower))
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

  const DictEntry found = usedEntries[static_cast<size_t>(foundIndex)];

  // Posting lists are concatenated. To get this term's end offset,
  // find the next-greater posting offset from other terms.
  uint64_t nextPosting = static_cast<uint64_t>(sb.st_size - postingsStart);
  for (const DictEntry& e : usedEntries)
  {
    if (e.postingOffset > found.postingOffset && e.postingOffset < nextPosting)
      nextPosting = e.postingOffset;
  }

  size_t startOff = postingsStart + static_cast<size_t>(found.postingOffset);
  size_t endOff = postingsStart + static_cast<size_t>(nextPosting);
  if (startOff + 16 > endOff || endOff > static_cast<size_t>(sb.st_size))
  {
    std::fprintf(stderr, "Corrupt posting list boundaries.\n");
    std::free(queryLower);
    munmap(data, sb.st_size);
    return 1;
  }

  // Each posting list begins with two u64 values currently written by indexer:
  // [document frequency][collection frequency placeholder].
  const uint8_t* posting = data + startOff;
  uint64_t documentFrequency = read_u64_le(posting);
  uint64_t collectionFrequency = read_u64_le(posting + 8);
  const uint8_t* cursor = posting + 16;
  const uint8_t* postingEnd = data + endOff;

  // Remaining bytes are VLQ deltas for token positions.
  uint32_t pos = 0;
  size_t decoded = 0;

  std::printf("Term: %s\n", queryLower);
  std::printf("Document frequency: %llu\n", static_cast<unsigned long long>(documentFrequency));
  std::printf("Collection frequency (raw field): %llu\n",
              static_cast<unsigned long long>(collectionFrequency));
  std::printf("Positions preview:");

  while (cursor < postingEnd)
  {
    const uint8_t* next = nullptr;
    uint32_t delta = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      break;
    cursor = next;
    pos += delta;
    if (decoded < 25)
      std::printf(" %u", pos);
    decoded++;
  }

  std::printf("\n");
  std::printf("Decoded positions: %zu\n", decoded);

  std::free(queryLower);
  munmap(data, sb.st_size);
  return 0;
}
