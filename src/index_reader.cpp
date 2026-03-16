#include "avengine/string.h"
#include "index.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct DictEntry
{
  uint64_t stringOffset;
  uint64_t postingOffset;
};

struct DecodedPosting
{
  avengine::vector<uint32_t> previewPositions;
  size_t decodedCount = 0;
  const uint8_t* next = nullptr;
  bool ok = false;
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
    const uint8_t byte = *begin++;
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
    const unsigned char c = static_cast<unsigned char>(src[i]);
    dst[i] = static_cast<char>(isalpha(c) ? tolower(c) : c);
    i++;
  }
  dst[i] = '\0';
}

static DecodedPosting decode_posting(const uint8_t* cursor, const uint8_t* postingEnd,
                                     uint64_t collectionFrequency)
{
  DecodedPosting out;
  uint32_t pos = 0;

  for (uint64_t i = 0; i < collectionFrequency; i++)
  {
    const uint8_t* next = nullptr;
    const uint32_t delta = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      return out;

    cursor = next;
    pos += delta;
    if (out.previewPositions.size() < 25)
      out.previewPositions.pushBack(pos);
    out.decodedCount++;
  }

  out.next = cursor;
  out.ok = true;
  return out;
}

static void print_position_preview(const avengine::vector<uint32_t>& preview)
{
  printf("  Positions preview:");
  if (preview.empty())
  {
    printf(" (none)\n");
    return;
  }

  for (size_t i = 0; i < preview.size(); i++)
    printf(" %u", preview[i]);
  printf("\n");
}

static void usage(const char* program)
{
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s <index.sfi> --list\n", program);
  fprintf(stderr, "  %s <index.sfi> <term>\n", program);
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
    perror("open");
    return 1;
  }

  struct stat sb = {};
  if (fstat(fd, &sb) != 0)
  {
    perror("fstat");
    close(fd);
    return 1;
  }

  if (sb.st_size < 32)
  {
    fprintf(stderr, "Index too small.\n");
    close(fd);
    return 1;
  }

  uint8_t* data = static_cast<uint8_t*>(mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (data == MAP_FAILED)
  {
    perror("mmap");
    return 1;
  }

  const uint64_t magic = read_u64_le(data);
  const uint64_t version = read_u64_le(data + 8);
  if (magic != INDEX_FILE_MAGIC || version != INDEX_FILE_VERSION)
  {
    fprintf(stderr, "Unsupported index format.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  const uint64_t documentCount = read_u64_le(data + 16);
  const uint64_t uniqueWordsPlusOne = read_u64_le(data + 24);
  const uint64_t tableSlots = uniqueWordsPlusOne * 2;
  const size_t headerBytes = 32;
  const uint64_t dictionaryBytes = headerBytes + tableSlots * 16;

  if (dictionaryBytes > static_cast<uint64_t>(sb.st_size))
  {
    fprintf(stderr, "Corrupt index: dictionary exceeds file size.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  avengine::vector<DictEntry> entries;
  for (uint64_t i = 0; i < tableSlots; i++)
  {
    const size_t offset = headerBytes + static_cast<size_t>(i * 16);
    entries.pushBack({read_u64_le(data + offset), read_u64_le(data + offset + 8)});
  }

  const uint8_t* stringStart = data + dictionaryBytes;
  const uint8_t* fileEnd = data + sb.st_size;

  size_t inferredStringBytes = 0;
  avengine::vector<DictEntry> usedEntries;
  avengine::vector<avengine::string> terms;

  for (size_t i = 0; i < entries.size(); i++)
  {
    const DictEntry& entry = entries[i];
    if (entry.stringOffset == UINT64_MAX && entry.postingOffset == UINT64_MAX)
      continue;
    if (dictionaryBytes + entry.stringOffset >= static_cast<uint64_t>(sb.st_size))
      continue;

    const uint8_t* termPtr = stringStart + entry.stringOffset;
    const size_t len = cstr_len_safe(termPtr, fileEnd);
    if (len == static_cast<size_t>(-1))
      continue;

    const size_t endOff = static_cast<size_t>(entry.stringOffset + len + 1);
    if (endOff > inferredStringBytes)
      inferredStringBytes = endOff;

    usedEntries.pushBack(entry);
    terms.pushBack(avengine::string(reinterpret_cast<const char*>(termPtr)));
  }

  const size_t postingsStart = static_cast<size_t>(dictionaryBytes) + inferredStringBytes;
  if (postingsStart > static_cast<size_t>(sb.st_size))
  {
    fprintf(stderr, "Corrupt index: postings start exceeds file size.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  printf("Format: v4\n");
  printf("Documents: %llu\n", static_cast<unsigned long long>(documentCount));
  printf("Dictionary slots: %llu\n", static_cast<unsigned long long>(tableSlots));
  printf("Parsed terms (best effort): %zu\n", terms.size());

  if (listMode)
  {
    for (size_t i = 0; i < terms.size(); i++)
      printf("%s\n", terms[i].cstr());
    munmap(data, sb.st_size);
    return 0;
  }

  const size_t rawLen = cstr_len(argv[2]);
  char* queryLower = static_cast<char*>(malloc(rawLen + 1));
  if (!queryLower)
  {
    fprintf(stderr, "Out of memory.\n");
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
    printf("Term '%s' not found.\n", queryLower);
    free(queryLower);
    munmap(data, sb.st_size);
    return 0;
  }

  const DictEntry found = usedEntries[static_cast<size_t>(foundIndex)];

  uint64_t nextPosting = static_cast<uint64_t>(sb.st_size - postingsStart);
  for (size_t i = 0; i < usedEntries.size(); i++)
  {
    const DictEntry& entry = usedEntries[i];
    if (entry.postingOffset > found.postingOffset && entry.postingOffset < nextPosting)
      nextPosting = entry.postingOffset;
  }

  const size_t startOff = postingsStart + static_cast<size_t>(found.postingOffset);
  const size_t endOff = postingsStart + static_cast<size_t>(nextPosting);
  if (startOff + 16 > endOff || endOff > static_cast<size_t>(sb.st_size))
  {
    fprintf(stderr, "Corrupt v4 posting list boundaries.\n");
    free(queryLower);
    munmap(data, sb.st_size);
    return 1;
  }

  const uint8_t* posting = data + startOff;
  const uint8_t* postingEnd = data + endOff;
  const uint64_t documentFrequency = read_u64_le(posting);
  const uint64_t collectionFrequency = read_u64_le(posting + 8);

  const DecodedPosting decoded = decode_posting(posting + 16, postingEnd, collectionFrequency);
  if (!decoded.ok)
  {
    fprintf(stderr, "Corrupt v4 field encoding.\n");
    free(queryLower);
    munmap(data, sb.st_size);
    return 1;
  }

  printf("Term: %s\n", queryLower);
  printf("Document frequency: %llu\n", static_cast<unsigned long long>(documentFrequency));
  printf("Collection frequency: %llu\n", static_cast<unsigned long long>(collectionFrequency));
  print_position_preview(decoded.previewPositions);
  printf("Decoded positions: %zu\n", decoded.decodedCount);

  if (decoded.next != postingEnd)
    printf("Note: %zu trailing posting bytes were not consumed.\n",
           static_cast<size_t>(postingEnd - decoded.next));

  free(queryLower);
  munmap(data, sb.st_size);
  return 0;
}
