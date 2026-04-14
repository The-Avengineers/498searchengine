// Post-crawl offline tool that:
//   1. Memory-maps ac_log.tsv and builds an
//      unordered_map<vector<string_slice>> keyed by target URL.  The map
//      values are zero-copy views into the mapped TSV buffer — no per-word
//      heap allocation occurs during this scan.
//   2. Walks every NNNNN.sfi shard in the CWD, extracts each document's URL
//      from the DOCUMENT_END posting, looks it up in the map from step 1,
//      tokenises the anchor words (lowercase alpha runs), and emits them into
//      a new Index that is serialised as anchor_NNNNN.sfi.
//
// The TSV mapping is kept alive for the full duration of step 2 so that the
// string_slice values (which point directly into the mapped buffer) remain
// valid.

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/string_slice.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/indexer/indexer.h>
#include <avengine/unordered_map.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Binary-format helpers (match the on-disk encoding written by indexer.cpp)
static uint64_t readU64Le(const uint8_t* p)
{
  uint64_t v = 0;
  for (int i = 0; i < 8; i++)
    v |= static_cast<uint64_t>(p[i]) << (8 * i);
  return v;
}

static uint32_t readVarU32(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  uint32_t result = 0, shift = 0;
  while (begin < end && shift < 35)
  {
    uint8_t b = *begin++;
    result |= static_cast<uint32_t>(b & 0x7Fu) << shift;
    if (!(b & 0x80u))
    {
      *next = begin;
      return result;
    }
    shift += 7;
  }
  *next = nullptr;
  return 0;
}

// Reads a u64-length-prefixed string (as written by Writer::writeString):
// [len u64][len raw bytes, no null terminator].
// Advances *next past the consumed bytes; sets *next = nullptr on failure.
static avengine::string readLPString(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  if (begin + 8 > end)
  {
    *next = nullptr;
    return avengine::string();
  }

  const uint64_t len64 = readU64Le(begin);
  if (len64 > static_cast<uint64_t>(end - (begin + 8)))
  {
    *next = nullptr;
    return avengine::string();
  }

  const uint8_t* payload = begin + 8;
  const size_t len = static_cast<size_t>(len64);
  avengine::string s;
  for (size_t i = 0; i < len; i++)
    s.pushBack(static_cast<char>(payload[i]));
  *next = payload + len;
  return s;
}

// Directory scan, collects every NNNNN.sfi file in the CWD
static void scanSfiFiles(avengine::vector<avengine::string>& out)
{
  DIR* dir = opendir(".");
  if (!dir)
  {
    std::perror("opendir(\".\")");
    return;
  }

  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr)
  {
    const char* name = ent->d_name;
    size_t len = 0;

    while (name[len])
    {
      len++;
    }

    // Exact form: NNNNN.sfi (9 characters)
    if (len != 9)
    {
      continue;
    }

    bool allDigits = true;
    for (int i = 0; i < 5; i++)
    {
      if (name[i] < '0' || name[i] > '9')
      {
        allDigits = false;
        break;
      }
    }

    if (!allDigits)
    {
      continue;
    }

    if (name[5] != '.' || name[6] != 's' || name[7] != 'f' || name[8] != 'i')
    {
      continue;
    }

    out.pushBack(avengine::string(name));
  }

  closedir(dir);
}

// Phase 1: Scan TSV → unordered_map<vector<string_slice>>
//
// Each line of ac_log.tsv has the form:
//   <targetUrl> TAB <word1 word2 ...>
//
// Multiple lines may share the same target URL; words from all matching lines
// are accumulated into the same vector entry.
//
// The string_slices stored as values are zero-copy views into `content` (i.e.
// the mmap'd TSV buffer).  The caller must keep that buffer alive as long as
// the map is in use.

static void buildTsvMap(avengine::string_slice content,
                        avengine::unordered_map<avengine::vector<avengine::string_slice>>& out,
                        size_t& urlCount)
{
  for (auto line : content.split('\n'))
  {
    if (line.empty())
    {
      continue;
    }

    // Locate the first TAB to separate the URL from the word payload.
    size_t tabPos = avengine::string_slice::npos;
    for (size_t ci = 0; ci < line.size(); ci++)
    {
      if (line[ci] == '\t')
      {
        tabPos = ci;
        break;
      }
    }

    avengine::string_slice urlSlice =
        (tabPos == avengine::string_slice::npos) ? line : line.substr(0, tabPos);

    if (urlSlice.empty())
    {
      continue;
    }

    // Look up (or create) the word vector for this URL.
    // NOTE: Bucket nodes are heap-allocated and are never relocated during a
    // rehash — only their links in the bucket array change — so the pointer
    // returned by get() is stable across subsequent insert() calls.
    avengine::vector<avengine::string_slice>* bucket = out.get(urlSlice);
    if (!bucket)
    {
      out.insert(urlSlice.toString(), avengine::vector<avengine::string_slice>());
      bucket = out.get(avengine::string_slice(urlSlice));
      urlCount++;
    }

    // Nothing to add if there is no word payload on this line.
    if (tabPos == avengine::string_slice::npos || tabPos + 1 >= line.size())
    {
      continue;
    }

    // Split the word payload on spaces and store each non-empty word as a
    // string_slice pointing directly into the mmap'd buffer.
    avengine::string_slice wordsSlice = line.substr(tabPos + 1, line.size() - tabPos - 1);
    avengine::vector<avengine::string_slice> words = wordsSlice.split(' ');
    for (size_t wi = 0; wi < words.size(); wi++)
    {
      avengine::string_slice w = words[wi].trim();
      if (!w.empty())
      {
        bucket->pushBack(w);
      }
    }
  }
}

// Parses the DOCUMENT_END posting list from a memory-mapped SFI blob and
// fills `docMetas` with one DocMeta per document (in declaration order).
//
// Current top-level header:
//   [magic u64][doc_count u64][unique_words u64][token_offset u64][input_size u64]
//   then `unique_words` term records:
//     [word string(u64-len + bytes)][positions_count u64][doc_freq u64][postings_offset u64]
//
// DOCUMENT_END posting blob layout (see encodePostingList in indexer.cpp):
//   [doc_freq u64][coll_freq u64][skip_count u64]
//   [skip stream:     skip_count × 3 varints (posIdx, posVal, byteOff)]
//   [position stream: coll_freq varints, delta-encoded]
//   [metadata:        doc_freq × (url string, title string, description string,
//                                   8-byte RankingInput)]
static bool parseDocEnd(const uint8_t* data, size_t fileSize,
                        avengine::vector<DocumentMetadata>& docMetas)
{
  constexpr uint64_t kIndexMagic = 0x008FB8EF84979FF0ULL;
  if (fileSize < 5 * sizeof(uint64_t))
    return false;

  const uint64_t magic = readU64Le(data);
  if (magic != kIndexMagic)
    return false;

  const uint64_t uniqueWords = readU64Le(data + 16);
  const size_t fixedHeader = 5 * sizeof(uint64_t);
  const uint8_t* cursor = data + fixedHeader;
  const uint8_t* fileEnd = data + fileSize;

  struct DEntry
  {
    avengine::string term;
    uint64_t postOff;
  };
  avengine::vector<DEntry> entries;

  for (uint64_t i = 0; i < uniqueWords; ++i)
  {
    const uint8_t* nx = nullptr;
    avengine::string term = readLPString(cursor, fileEnd, &nx);
    if (!nx)
      return false;
    cursor = nx;

    if (cursor + 24 > fileEnd)
      return false;

    (void)readU64Le(cursor);     // positions_count
    (void)readU64Le(cursor + 8); // document_frequency
    const uint64_t postOff = readU64Le(cursor + 16);
    cursor += 24;

    entries.pushBack({term, postOff});
  }

  const size_t postingsStart = static_cast<size_t>(cursor - data);
  if (postingsStart > fileSize)
    return false;

  ssize_t deIdx = -1;
  for (size_t i = 0; i < entries.size(); i++)
  {
    if (entries[i].term == avengine::string(DOCEND_TOKEN))
    {
      deIdx = static_cast<ssize_t>(i);
      break;
    }
  }
  if (deIdx < 0)
    return false;

  const DEntry& de = entries[static_cast<size_t>(deIdx)];

  uint64_t nextPost = static_cast<uint64_t>(fileSize - postingsStart);
  for (size_t i = 0; i < entries.size(); i++)
  {
    if (entries[i].postOff > de.postOff && entries[i].postOff < nextPost)
      nextPost = entries[i].postOff;
  }

  size_t blobStart = postingsStart + static_cast<size_t>(de.postOff);
  size_t blobEnd = postingsStart + static_cast<size_t>(nextPost);
  if (blobStart + 24 > blobEnd || blobEnd > fileSize)
    return false;

  const uint8_t* p = data + blobStart;
  const uint8_t* pEnd = data + blobEnd;

  uint64_t docFreq = readU64Le(p);
  uint64_t collFreq = readU64Le(p + 8);
  uint64_t skipCount = readU64Le(p + 16);
  cursor = p + 24;

  for (uint64_t s = 0; s < skipCount; s++)
  {
    for (int f = 0; f < 3; f++)
    {
      const uint8_t* nx = nullptr;
      readVarU32(cursor, pEnd, &nx);
      if (!nx)
        return false;
      cursor = nx;
    }
  }

  for (uint64_t pos = 0; pos < collFreq; pos++)
  {
    const uint8_t* nx = nullptr;
    readVarU32(cursor, pEnd, &nx);
    if (!nx)
      return false;
    cursor = nx;
  }

  for (uint64_t d = 0; d < docFreq; d++)
  {
    const uint8_t* nx = nullptr;

    avengine::string url = readLPString(cursor, pEnd, &nx);
    if (!nx)
      return false;
    cursor = nx;

    avengine::string title = readLPString(cursor, pEnd, &nx);
    if (!nx)
      return false;
    cursor = nx;

    avengine::string description = readLPString(cursor, pEnd, &nx);
    if (!nx)
      return false;
    cursor = nx;

    RankingInput rankInput = {};
    if (cursor + sizeof(RankingInput) <= pEnd)
    {
      memcpy(&rankInput, cursor, sizeof(RankingInput));
      cursor += sizeof(RankingInput);
    }
    else
    {
      cursor = pEnd;
    }

    docMetas.pushBack({url, title, description, rankInput});
  }

  return true;
}

// Phase 2: Walk every NNNNN.sfi shard and build anchor_NNNNN.sfi output files
static void processShards(const avengine::vector<avengine::string>& sfiFiles,
                          avengine::unordered_map<avengine::vector<avengine::string_slice>>& tsvMap,
                          size_t& filesWritten)
{
  Index* anchorIndex = new Index();
  size_t anchorCounter = 0;

  for (size_t si = 0; si < sfiFiles.size(); si++)
  {
    const char* filename = sfiFiles[si].cstr();

    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
      std::perror(filename);
      continue;
    }

    struct stat sb;
    if (fstat(fd, &sb) != 0)
    {
      std::perror("fstat");
      close(fd);
      continue;
    }

    if (sb.st_size < 16)
    {
      close(fd);
      continue;
    }

    uint8_t* data = static_cast<uint8_t*>(
        mmap(nullptr, static_cast<size_t>(sb.st_size), PROT_READ, MAP_PRIVATE, fd, 0));

    close(fd);
    if (data == MAP_FAILED)
    {
      std::perror("mmap");
      continue;
    }

    // Extract per-document metadata from the DOCUMENT_END posting.
    avengine::vector<DocumentMetadata> docMetas;
    if (!parseDocEnd(data, static_cast<size_t>(sb.st_size), docMetas))
    {
      std::fprintf(stderr, "Warning: %s: could not parse DOCUMENT_END, skipping.\n", filename);
      munmap(data, static_cast<size_t>(sb.st_size));
      continue;
    }

    // Walk each document; if anchor text exists for its URL, emit it.
    for (size_t di = 0; di < docMetas.size(); di++)
    {
      DocumentMetadata& doc = docMetas[di];

      avengine::vector<avengine::string_slice>* words = tsvMap.get(avengine::string_slice(doc.url));

      if (!words)
      {
        continue;
      }

      DocumentMetadata meta;
      meta.url = doc.url;
      meta.title = doc.title;
      meta.description = doc.description;
      meta.input = static_cast<RankingInput>(doc.input);

      anchorIndex->beginDocument(meta);
      tokenizeInto(*words, *anchorIndex, PhraseKind::BODY);
      anchorIndex->endDocument();

      // Flush to a new shard whenever the index exceeds the size threshold.
      if (anchorIndex->offset >= OFFSET_THRESHOLD)
      {
        char outName[32];
        snprintf(outName, sizeof(outName), "anchor_%05zu.sfi", anchorCounter);
        int ofd = open(outName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (ofd >= 0)
        {
          anchorIndex->serialize(ofd);
          close(ofd);
          filesWritten++;
        }
        else
        {
          std::perror(outName);
        }

        delete anchorIndex;
        anchorIndex = new Index();
        anchorCounter++;
      }
    }

    munmap(data, static_cast<size_t>(sb.st_size));
  }

  // Flush the final (possibly partial) shard.
  if (anchorIndex->totalUniqWords > 0)
  {
    char outName[32];
    snprintf(outName, sizeof(outName), "anchor_%05zu.sfi", anchorCounter);
    int ofd = open(outName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd >= 0)
    {
      anchorIndex->serialize(ofd);
      close(ofd);
      filesWritten++;
    }
    else
    {
      std::perror(outName);
    }
  }

  delete anchorIndex;
}

int main()
{
  std::printf("[anchor_processor] Starting post-crawl anchor text indexing.\n");

  // Locate source .sfi shards.
  avengine::vector<avengine::string> sfiFiles;
  scanSfiFiles(sfiFiles);
  std::printf("[anchor_processor] Found %zu source .sfi file(s).\n", sfiFiles.size());

  if (sfiFiles.size() == 0)
  {
    std::fprintf(stderr, "[anchor_processor] No .sfi files found; nothing to do.\n");
    return 0;
  }

  // Memory-map ac_log.tsv.  This mapping is kept alive until after
  // processShards() returns, because the tsvMap values are string_slice views
  // that point directly into this buffer.
  int tsvFd = open("ac_log.tsv", O_RDONLY);
  if (tsvFd < 0)
  {
    std::perror("open(\"ac_log.tsv\")");
    return 1;
  }

  struct stat tsvStat;
  if (fstat(tsvFd, &tsvStat) != 0)
  {
    std::perror("fstat(ac_log.tsv)");
    close(tsvFd);
    return 1;
  }

  if (tsvStat.st_size == 0)
  {
    std::fprintf(stderr, "[anchor_processor] ac_log.tsv is empty; nothing to do.\n");
    close(tsvFd);
    return 0;
  }

  void* tsvRaw = mmap(nullptr, static_cast<size_t>(tsvStat.st_size), PROT_READ | PROT_WRITE,
                      MAP_PRIVATE, tsvFd, 0);
  close(tsvFd);
  if (tsvRaw == MAP_FAILED)
  {
    std::perror("mmap(ac_log.tsv)");
    return 1;
  }

  avengine::string_slice tsvContent(static_cast<char*>(tsvRaw),
                                    static_cast<size_t>(tsvStat.st_size));

  // Phase 1: build URL → anchor-word map from the TSV.
  avengine::unordered_map<avengine::vector<avengine::string_slice>> tsvMap;
  size_t urlCount = 0;
  buildTsvMap(tsvContent, tsvMap, urlCount);
  std::printf("[anchor_processor] TSV: accumulated anchor text for %zu URL(s).\n", urlCount);

  // Phase 2: walk each shard, look up URLs, emit anchor index shards.
  size_t filesWritten = 0;
  processShards(sfiFiles, tsvMap, filesWritten);

  // Safe to release the TSV mapping only after processShards() has finished,
  // because tsvMap holds string_slices that point into tsvRaw.
  munmap(tsvRaw, static_cast<size_t>(tsvStat.st_size));

  std::printf("[anchor_processor] Done. Wrote %zu anchor_NNNNN.sfi file(s).\n", filesWritten);
  return 0;
}
