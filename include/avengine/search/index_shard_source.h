#pragma once

#include <avengine/constraint_solver/isr_source.h>
#include <avengine/index_stream_reader.h>
#include <avengine/unordered_map.h>

namespace avengine
{
namespace search
{

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

  string GetPath() const;
  string GetDocumentURL(uint32_t docId) const override;
  string GetDocumentTitle(uint32_t docId) const;
  const DocumentInfo* GetDocumentInfo(uint32_t docId) const;
  bool DecodeTermOccurrences(const string& term, vector<Location>* positions,
                             uint32_t* documentFrequency) const;
  ISRWord* BuildWordIsr(const vector<Location>& positions, uint32_t documentFrequency) const;
  ISRWord* OpenTermIsr(const string& term) const override;
  ISREndDoc* OpenEndDocIsr() const override;

private:
  struct DictEntry
  {
    uint64_t postingOffset = 0;
  };

  struct ParsedTerm
  {
    string Term;
    DictEntry Entry = {};
  };

  struct PostingSpan
  {
    size_t StartOffset = 0;
    size_t EndOffset = 0;
  };

  bool ParseDictionary(uint64_t uniqueWords);
  bool ParseDocumentMetadata();
  bool FindPostingBounds(const string& term, const uint8_t** begin, const uint8_t** end,
                         uint64_t* documentFrequency) const;
  void Reset();

  string path_;
  uint8_t* data_ = nullptr;
  size_t fileSize_ = 0;
  size_t postingsStart_ = 0;
  vector<ParsedTerm> terms_;
  unordered_map<PostingSpan> postingBounds_;
  vector<DocumentInfo> documents_;
  vector<string> documentUrls_;
  vector<string> documentTitles_;
  vector<string> documentDescriptions_;

  // Per-query cache: populated lazily by OpenTermIsr, cleared via ClearTermCache()
  // after each shard's work is done. Declared mutable so const OpenTermIsr can write it.
  mutable unordered_map<vector<Location>> termPositionCache_;
  mutable unordered_map<uint32_t> termDocFreqCache_;
};

} // namespace search
} // namespace avengine
