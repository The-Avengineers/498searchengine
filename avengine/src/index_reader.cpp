#include "avengine/string.h"
#include "index.h"
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

struct ParsedSkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

struct DecodedField
{
  avengine::vector<ParsedSkipPointer> skips;
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

static bool is_empty_dictionary_entry(const DictEntry& e, bool isV2)
{
  if (isV2)
    return e.stringOffset == UINT64_MAX && e.postingOffset == UINT64_MAX;

  return e.stringOffset == 0 && e.postingOffset == 0;
}

static DecodedField decode_field(const uint8_t* cursor, const uint8_t* postingEnd,
                                 uint64_t skipCount, uint64_t collectionFrequency)
{
  DecodedField out;

  for (uint64_t i = 0; i < skipCount; i++)
  {
    const uint8_t* next = nullptr;
    uint32_t positionIndex = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      return out;
    cursor = next;

    uint32_t positionValue = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      return out;
    cursor = next;

    uint32_t byteOffset = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      return out;
    cursor = next;

    out.skips.pushBack({positionIndex, positionValue, byteOffset});
  }

  uint32_t pos = 0;
  for (uint64_t i = 0; i < collectionFrequency; i++)
  {
    const uint8_t* next = nullptr;
    uint32_t delta = read_var_u32(cursor, postingEnd, &next);
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

static void print_skip_preview(const avengine::vector<ParsedSkipPointer>& skips)
{
  if (skips.empty())
  {
    std::printf("  Skip preview: (none)\n");
    return;
  }

  std::printf("  Skip preview:");
  for (size_t i = 0; i < skips.size() && i < 8; i++)
  {
    const ParsedSkipPointer& sp = skips[i];
    std::printf(" [idx=%u pos=%u off=%u]", sp.positionIndex, sp.positionValue, sp.byteOffset);
  }
  std::printf("\n");
}

static void print_position_preview(const avengine::vector<uint32_t>& preview)
{
  std::printf("  Positions preview:");
  if (preview.empty())
  {
    std::printf(" (none)\n");
    return;
  }

  for (uint32_t pos : preview)
    std::printf(" %u", pos);
  std::printf("\n");
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

  bool isV2 = false;
  uint64_t documentCount = 0;
  uint64_t uniqueWordsPlusOne = 0;
  uint64_t fieldCount = 1;
  uint64_t headerBytes = 16;

  if (sb.st_size >= 40)
  {
    const uint64_t magic = read_u64_le(data);
    const uint64_t version = read_u64_le(data + 8);
    if (magic == INDEX_FILE_MAGIC_V2 && version == INDEX_FILE_VERSION_V2)
    {
      isV2 = true;
      headerBytes = 40;
      documentCount = read_u64_le(data + 16);
      uniqueWordsPlusOne = read_u64_le(data + 24);
      fieldCount = read_u64_le(data + 32);
    }
  }

  if (!isV2)
  {
    documentCount = read_u64_le(data);
    uniqueWordsPlusOne = read_u64_le(data + 8);
    fieldCount = 1;
    headerBytes = 16;
  }

  const uint64_t tableSlots = uniqueWordsPlusOne * 2;
  const uint64_t dictionaryBytes = headerBytes + tableSlots * 16;

  if (dictionaryBytes > static_cast<uint64_t>(sb.st_size))
  {
    std::fprintf(stderr, "Corrupt index: dictionary exceeds file size.\n");
    munmap(data, sb.st_size);
    return 1;
  }

  avengine::vector<DictEntry> entries;
  for (uint64_t i = 0; i < tableSlots; i++)
  {
    size_t offset = static_cast<size_t>(headerBytes + i * 16);
    entries.pushBack({read_u64_le(data + offset), read_u64_le(data + offset + 8)});
  }

  const uint8_t* stringStart = data + dictionaryBytes;
  const uint8_t* fileEnd = data + sb.st_size;

  size_t inferredStringBytes = 0;
  size_t parsedTerms = 0;
  avengine::vector<DictEntry> usedEntries;
  avengine::vector<avengine::string> terms;

  for (const DictEntry& e : entries)
  {
    if (is_empty_dictionary_entry(e, isV2))
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

  std::printf("Format: %s\n", isV2 ? "v2" : "v1");
  std::printf("Documents: %llu\n", static_cast<unsigned long long>(documentCount));
  std::printf("Dictionary slots: %llu\n", static_cast<unsigned long long>(tableSlots));
  std::printf("Parsed terms (best effort): %zu\n", parsedTerms);
  if (isV2)
    std::printf("Fields: %llu\n", static_cast<unsigned long long>(fieldCount));

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

  uint64_t nextPosting = static_cast<uint64_t>(sb.st_size - postingsStart);
  for (const DictEntry& e : usedEntries)
  {
    if (e.postingOffset > found.postingOffset && e.postingOffset < nextPosting)
      nextPosting = e.postingOffset;
  }

  size_t startOff = postingsStart + static_cast<size_t>(found.postingOffset);
  size_t endOff = postingsStart + static_cast<size_t>(nextPosting);

  std::printf("Term: %s\n", queryLower);

  if (isV2)
  {
    if (startOff + 56 > endOff || endOff > static_cast<size_t>(sb.st_size))
    {
      std::fprintf(stderr, "Corrupt v2 posting list boundaries.\n");
      std::free(queryLower);
      munmap(data, sb.st_size);
      return 1;
    }

    const uint8_t* posting = data + startOff;
    const uint8_t* postingEnd = data + endOff;

    uint64_t documentFrequency = read_u64_le(posting);
    uint64_t normalCollectionFrequency = read_u64_le(posting + 8);
    uint64_t titleCollectionFrequency = read_u64_le(posting + 16);
    uint64_t anchorCollectionFrequency = read_u64_le(posting + 24);
    uint64_t normalSkipCount = read_u64_le(posting + 32);
    uint64_t titleSkipCount = read_u64_le(posting + 40);
    uint64_t anchorSkipCount = read_u64_le(posting + 48);

    const uint8_t* cursor = posting + 56;

    DecodedField normalField =
        decode_field(cursor, postingEnd, normalSkipCount, normalCollectionFrequency);
    if (!normalField.ok)
    {
      std::fprintf(stderr, "Corrupt v2 normal field encoding.\n");
      std::free(queryLower);
      munmap(data, sb.st_size);
      return 1;
    }

    DecodedField titleField =
        decode_field(normalField.next, postingEnd, titleSkipCount, titleCollectionFrequency);
    if (!titleField.ok)
    {
      std::fprintf(stderr, "Corrupt v2 title field encoding.\n");
      std::free(queryLower);
      munmap(data, sb.st_size);
      return 1;
    }

    DecodedField anchorField =
        decode_field(titleField.next, postingEnd, anchorSkipCount, anchorCollectionFrequency);
    if (!anchorField.ok)
    {
      std::fprintf(stderr, "Corrupt v2 anchor field encoding.\n");
      std::free(queryLower);
      munmap(data, sb.st_size);
      return 1;
    }

    std::printf("Document frequency: %llu\n", static_cast<unsigned long long>(documentFrequency));

    std::printf("Normal collection frequency: %llu\n",
                static_cast<unsigned long long>(normalCollectionFrequency));
    std::printf("Normal skip pointers: %zu\n", normalField.skips.size());
    print_skip_preview(normalField.skips);
    print_position_preview(normalField.previewPositions);
    std::printf("  Decoded positions: %zu\n", normalField.decodedCount);

    std::printf("Title collection frequency: %llu\n",
                static_cast<unsigned long long>(titleCollectionFrequency));
    std::printf("Title skip pointers: %zu\n", titleField.skips.size());
    print_skip_preview(titleField.skips);
    print_position_preview(titleField.previewPositions);
    std::printf("  Decoded positions: %zu\n", titleField.decodedCount);

    std::printf("Anchor collection frequency: %llu\n",
                static_cast<unsigned long long>(anchorCollectionFrequency));
    std::printf("Anchor skip pointers: %zu\n", anchorField.skips.size());
    print_skip_preview(anchorField.skips);
    print_position_preview(anchorField.previewPositions);
    std::printf("  Decoded positions: %zu\n", anchorField.decodedCount);

    if (anchorField.next != postingEnd)
      std::printf("Note: %zu trailing posting bytes were not consumed.\n",
                  static_cast<size_t>(postingEnd - anchorField.next));
  }
  else
  {
    if (startOff + 24 > endOff || endOff > static_cast<size_t>(sb.st_size))
    {
      std::fprintf(stderr, "Corrupt v1 posting list boundaries.\n");
      std::free(queryLower);
      munmap(data, sb.st_size);
      return 1;
    }

    const uint8_t* posting = data + startOff;
    const uint8_t* postingEnd = data + endOff;

    uint64_t documentFrequency = read_u64_le(posting);
    uint64_t collectionFrequency = read_u64_le(posting + 8);
    uint64_t skipCount = read_u64_le(posting + 16);

    DecodedField field = decode_field(posting + 24, postingEnd, skipCount, collectionFrequency);
    if (!field.ok)
    {
      std::fprintf(stderr, "Corrupt v1 field encoding.\n");
      std::free(queryLower);
      munmap(data, sb.st_size);
      return 1;
    }

    std::printf("Document frequency: %llu\n", static_cast<unsigned long long>(documentFrequency));
    std::printf("Collection frequency: %llu\n",
                static_cast<unsigned long long>(collectionFrequency));
    std::printf("Skip pointers: %zu\n", field.skips.size());
    print_skip_preview(field.skips);
    print_position_preview(field.previewPositions);
    std::printf("Decoded positions: %zu\n", field.decodedCount);

    if (field.next != postingEnd)
      std::printf("Note: %zu trailing posting bytes were not consumed.\n",
                  static_cast<size_t>(postingEnd - field.next));
  }

  std::free(queryLower);
  munmap(data, sb.st_size);
  return 0;
}
