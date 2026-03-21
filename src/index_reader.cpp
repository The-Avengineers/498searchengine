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

struct ParsedSkipPointer
{
  uint32_t positionIndex;
  uint32_t positionValue;
  uint32_t byteOffset;
};

struct DocInfo
{
  uint64_t endPosition; // global token offset of this doc's DOCUMENT_END marker
  avengine::string title;
  avengine::string url;
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

// Reads a varint-length-prefixed string (as written by append_string in index.cpp):
//   varint(len) followed by len raw bytes (no null terminator).
// Advances *next past the consumed bytes.
// On failure (truncated data) sets *next = nullptr and returns an empty string.
static avengine::string read_length_prefixed_string(const uint8_t* begin, const uint8_t* end,
                                                    const uint8_t** next)
{
  const uint8_t* after_len = nullptr;
  uint32_t len = read_var_u32(begin, end, &after_len);
  if (!after_len || after_len + len > end)
  {
    *next = nullptr;
    return avengine::string();
  }
  avengine::string result;
  for (uint32_t i = 0; i < len; i++)
    result.pushBack(static_cast<char>(after_len[i]));
  *next = after_len + len;
  return result;
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

// Parses the DOCUMENT_END posting list to extract per-document boundary positions and titles.
//
// The DOCUMENT_END posting blob layout (from encodePostingList in index.cpp):
//   [doc_freq u64][coll_freq u64][skip_count u64]
//   [skip stream: skip_count x (posIdx varint, posVal varint, byteOff varint)]
//   [position delta stream: coll_freq varints, delta-encoded from 0]
//   [metadata buffer: doc_freq x (url length-prefixed, title length-prefixed, 8-byte RankingInput)]
//
// Populates docInfos with one entry per document (endPosition, title), sorted ascending by
// endPosition. Returns true on success, false if DOCUMENT_END is absent or the blob is malformed.
static bool parse_docend_posting(const uint8_t* data, size_t fileSize, size_t postingsStart,
                                 const avengine::vector<DictEntry>& usedEntries,
                                 const avengine::vector<avengine::string>& terms,
                                 avengine::vector<DocInfo>& docInfos)
{
  // Locate the DOCUMENT_END entry in the dictionary.
  ssize_t docendIdx = -1;
  for (size_t i = 0; i < terms.size(); i++)
  {
    if (cstr_equal(terms[i].cstr(), "DOCUMENT_END"))
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

  const DictEntry& de = usedEntries[static_cast<size_t>(docendIdx)];

  // Determine the end of this posting blob by finding the smallest posting offset
  // that is strictly greater than de.postingOffset.
  uint64_t nextPosting = static_cast<uint64_t>(fileSize - postingsStart);
  for (const DictEntry& e : usedEntries)
  {
    if (e.postingOffset > de.postingOffset && e.postingOffset < nextPosting)
      nextPosting = e.postingOffset;
  }

  size_t startOff = postingsStart + static_cast<size_t>(de.postingOffset);
  size_t endOff = postingsStart + static_cast<size_t>(nextPosting);

  if (startOff + 24 > endOff || endOff > fileSize)
  {
    std::fprintf(stderr, "Warning: DOCUMENT_END posting list has corrupt boundaries.\n");
    return false;
  }

  const uint8_t* posting = data + startOff;
  const uint8_t* postingEnd = data + endOff;

  uint64_t docFreq = read_u64_le(posting);
  uint64_t collFreq = read_u64_le(posting + 8);
  uint64_t skipCount = read_u64_le(posting + 16);
  const uint8_t* cursor = posting + 24;

  // Consume the skip-pointer stream (3 varints per entry).
  for (uint64_t i = 0; i < skipCount; i++)
  {
    for (int field = 0; field < 3; field++)
    {
      const uint8_t* next = nullptr;
      read_var_u32(cursor, postingEnd, &next);
      if (!next)
      {
        std::fprintf(stderr, "Warning: Truncated skip pointer stream in DOCUMENT_END.\n");
        return false;
      }
      cursor = next;
    }
  }

  // Decode exactly collFreq position deltas to obtain per-document boundary positions.
  // For DOCUMENT_END, collFreq == docFreq (one end-marker per document).
  avengine::vector<uint64_t> boundaryPositions;
  {
    uint64_t pos = 0;
    for (uint64_t i = 0; i < collFreq; i++)
    {
      const uint8_t* next = nullptr;
      uint32_t delta = read_var_u32(cursor, postingEnd, &next);
      if (!next)
      {
        std::fprintf(stderr, "Warning: Truncated position stream in DOCUMENT_END.\n");
        return false;
      }
      cursor = next;
      pos += delta;
      boundaryPositions.pushBack(pos);
    }
  }

  // Read per-document metadata: url (discarded), title (kept), 8-byte RankingInput (skipped).
  for (uint64_t i = 0; i < docFreq; i++)
  {
    // URL — read and discard.
    const uint8_t* next = nullptr;
    avengine::string url = read_length_prefixed_string(cursor, postingEnd, &next);
    if (!next)
    {
      std::fprintf(stderr, "Warning: Truncated URL in DOCUMENT_END metadata at doc %llu.\n",
                   static_cast<unsigned long long>(i));
      return false;
    }
    cursor = next;

    // Title — keep.
    avengine::string title = read_length_prefixed_string(cursor, postingEnd, &next);
    if (!next)
    {
      std::fprintf(stderr, "Warning: Truncated title in DOCUMENT_END metadata at doc %llu.\n",
                   static_cast<unsigned long long>(i));
      return false;
    }
    cursor = next;

    // RankingInput — 8 bytes, skip.
    if (cursor + 8 > postingEnd)
    {
      std::fprintf(stderr,
                   "Warning: Truncated RankingInput in DOCUMENT_END metadata at doc %llu.\n",
                   static_cast<unsigned long long>(i));
      return false;
    }
    cursor += 8;

    uint64_t endPos = (i < boundaryPositions.size()) ? boundaryPositions[i] : UINT64_MAX;
    docInfos.pushBack({endPos, title, url});
  }

  return true;
}

// Returns the index of the first DocInfo whose endPosition is strictly greater than pos.
// Since DOCUMENT_END is ingested after all tokens in a document, every token position within
// document d satisfies pos < docInfos[d].endPosition, making this an upper-bound search.
// Returns docInfos.size() if no such entry exists (corrupt / position out of range).
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

  // Header: [document_count u64][unique_words_plus_one u64]
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

  // Dictionary is a flat hash table; each slot is [string_offset u64][posting_offset u64].
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

  // Scan non-empty dictionary slots, recover C strings from string buffer,
  // and infer where the postings region begins.
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

  // Determine the end of this posting blob.
  uint64_t nextPosting = static_cast<uint64_t>(sb.st_size - postingsStart);
  for (const DictEntry& e : usedEntries)
  {
    if (e.postingOffset > found.postingOffset && e.postingOffset < nextPosting)
      nextPosting = e.postingOffset;
  }

  size_t startOff = postingsStart + static_cast<size_t>(found.postingOffset);
  size_t endOff = postingsStart + static_cast<size_t>(nextPosting);
  if (startOff + 24 > endOff || endOff > static_cast<size_t>(sb.st_size))
  {
    std::fprintf(stderr, "Corrupt posting list boundaries.\n");
    std::free(queryLower);
    munmap(data, sb.st_size);
    return 1;
  }

  // Per posting-list header: [doc_freq u64][collection_freq u64][skip_count u64].
  const uint8_t* posting = data + startOff;
  uint64_t documentFrequency = read_u64_le(posting);
  uint64_t collectionFrequency = read_u64_le(posting + 8);
  uint64_t skipCount = read_u64_le(posting + 16);
  const uint8_t* cursor = posting + 24;
  const uint8_t* postingEnd = data + endOff;

  avengine::vector<ParsedSkipPointer> parsedSkips;
  for (uint64_t i = 0; i < skipCount; i++)
  {
    const uint8_t* next = nullptr;

    uint32_t positionIndex = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      break;
    cursor = next;

    uint32_t positionValue = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      break;
    cursor = next;

    uint32_t byteOffset = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      break;
    cursor = next;

    parsedSkips.pushBack({positionIndex, positionValue, byteOffset});
  }

  // Parse DOCUMENT_END to build the per-document info table (endPosition + title).
  avengine::vector<DocInfo> docInfos;
  parse_docend_posting(data, static_cast<size_t>(sb.st_size), postingsStart, usedEntries, terms,
                       docInfos);

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

  // Decode position delta stream and print each position with the containing document's title.
  size_t decoded = 0;
  uint64_t pos64 = 0;

  std::printf("Positions:\n");
  while (cursor < postingEnd)
  {
    const uint8_t* next = nullptr;
    uint32_t delta = read_var_u32(cursor, postingEnd, &next);
    if (!next)
      break;
    cursor = next;
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
