#pragma once

#include <avengine/constraint_solver/isr_source.h>
#include <avengine/index_stream_reader.h>
#include <avengine/unordered_map.h>

namespace avengine
{
namespace search
{

enum class ShardLoadCacheStatus
{
  NotLoaded,
  Hit,
  Rebuilt,
  RebuiltWithoutWrite,
};

class IndexShardSource : public constraint_solver::IsrSource
{
public:
  IndexShardSource() = default;
  explicit IndexShardSource(const string& path);
  ~IndexShardSource() override;

  IndexShardSource(const IndexShardSource&) = delete;
  IndexShardSource& operator=(const IndexShardSource&) = delete;

  bool Load(const string& path);
  bool IsLoaded() const;
  ShardLoadCacheStatus GetCacheStatus() const;

  string GetPath() const;
  string GetDocumentURL(uint32_t docId) const override;
  string GetDocumentTitle(uint32_t docId) const;
  string GetDocumentDescription(uint32_t docId) const;
  const DocumentInfo* GetDocumentInfo(uint32_t docId) const;
  bool DecodeTermOccurrences(const string& term, vector<Location>* positions,
                             uint32_t* documentFrequency) const;
  ISRWord* BuildWordIsr(const vector<Location>& positions, uint32_t documentFrequency) const;
  ISRWord* OpenTermIsr(const string& term) const override;
  ISREndDoc* OpenEndDocIsr() const override;

private:
  // Points into the mmap'd data_ region — no heap copy of the string bytes.
  struct DocSlice
  {
    uint32_t offset = 0;
    uint32_t len = 0;
  };

  // One entry per unique term: file offsets for both the term bytes and its
  // posting data. Stored sorted by term string so FindPostingBounds can binary-search.
  struct TermIndex
  {
    uint32_t termOffset = 0;  // byte offset in data_ to start of term content
    uint32_t termLen = 0;
    size_t startOffset = 0;   // posting data start offset in file
    size_t endOffset = 0;     // posting data end offset in file
  };

  struct SourceFingerprint
  {
    uint64_t fileSize = 0;
    uint64_t mtimeSec = 0;
    uint64_t mtimeNsec = 0;
    uint64_t documentCount = 0;
    uint64_t uniqueWords = 0;
    uint64_t tokenOffset = 0;
    uint64_t rankingInputSize = 0;
  };

  bool ParseDictionary(uint64_t uniqueWords);
  bool ParseDocumentMetadata();
  bool LoadDerivedCache(const SourceFingerprint& fingerprint);
  bool WriteDerivedCache(const SourceFingerprint& fingerprint) const;
  bool FindPostingBounds(const string& term, const uint8_t** begin, const uint8_t** end,
                         uint64_t* documentFrequency) const;
  void Reset();

  string path_;
  uint8_t* data_ = nullptr;
  size_t fileSize_ = 0;
  size_t postingsStart_ = 0;
  vector<TermIndex> termIndex_;
  vector<DocumentInfo> documents_;
  vector<DocSlice> docUrls_;
  vector<DocSlice> docTitles_;
  vector<DocSlice> docDescriptions_;
  ShardLoadCacheStatus cacheStatus_ = ShardLoadCacheStatus::NotLoaded;

  // Per-query cache: populated lazily by OpenTermIsr, cleared via ClearTermCache()
  // after each shard's work is done. Declared mutable so const OpenTermIsr can write it.
  mutable unordered_map<vector<Location>> termPositionCache_;
  mutable unordered_map<uint32_t> termDocFreqCache_;
};

} // namespace search
} // namespace avengine
