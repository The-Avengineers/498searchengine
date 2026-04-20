#pragma once

#include <avengine/indexer/indexer.h>
#include <avengine/string.h>
#include <avengine/vector.h>

#include <cstddef>
#include <cstdint>

namespace avengine
{
namespace search
{

struct LegacyTermInfo
{
  string term;
  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
};

struct LegacyDocumentMetadata
{
  uint32_t docId = 0;
  uint32_t startLocation = 0;
  uint32_t endLocation = 0;
  uint32_t docLength = 0;
  string url;
  string title;
  string description;
  RankingInput rankingInput = {};
};

struct LegacyDocTermStats
{
  uint32_t docId = 0;
  uint32_t termFrequency = 0;
};

struct LegacyDocTermPositions
{
  uint32_t docId = 0;
  vector<uint32_t> positions;
};

// Read-only parser for current .sfi shards used by the serving rebuild tool.
// This is intentionally separate from IndexShardSource: rebuild needs full term
// enumeration and doc-oriented reconstruction, while IndexShardSource is tuned
// for query-time ISR lookup.
class LegacyShardReader
{
public:
  LegacyShardReader() = default;
  explicit LegacyShardReader(const string& path);
  ~LegacyShardReader();

  LegacyShardReader(const LegacyShardReader&) = delete;
  LegacyShardReader& operator=(const LegacyShardReader&) = delete;

  bool Load(const string& path);
  void Reset();
  bool IsLoaded() const;
  string GetError() const;

  size_t TermCount() const;
  size_t DocumentCount() const;
  const LegacyTermInfo* GetTerm(size_t index) const;
  const LegacyDocumentMetadata* GetDocument(uint32_t docId) const;
  bool DecodeTermDocumentStats(const string& term, vector<LegacyDocTermStats>* out) const;
  bool DecodeTermDocumentPositions(const string& term, vector<LegacyDocTermPositions>* out) const;

private:
  struct TermEntry
  {
    LegacyTermInfo info;
    size_t postingStart = 0;
    size_t postingEnd = 0;
    uint64_t skipCount = 0;
  };

  bool ParseHeaderAndDictionary(uint64_t* documentCount);
  bool ParseDocumentMetadata(uint64_t expectedDocumentCount);
  const TermEntry* FindTerm(const string& term) const;
  void SetError(const char* message);
  void SetErrnoError(const char* operation, const string& path);

  string path_;
  string error_;
  uint8_t* data_ = nullptr;
  size_t fileSize_ = 0;
  bool loaded_ = false;
  vector<TermEntry> terms_;
  vector<LegacyDocumentMetadata> documents_;
};

} // namespace search
} // namespace avengine
