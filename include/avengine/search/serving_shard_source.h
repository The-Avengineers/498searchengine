#pragma once

#include <avengine/search/serving_index_format.h>
#include <avengine/string.h>
#include <avengine/vector.h>

#include <cstddef>
#include <cstdint>

namespace avengine
{
namespace search
{

struct ServingDecodedPosting
{
  uint32_t docId = 0;
  uint32_t bodyTf = 0;
  uint32_t titleTf = 0;
  uint32_t anchorTf = 0;
  uint32_t tierId = 0;
};

struct ServingTermView
{
  const ServingTermInfoV1* termInfo = nullptr;
  const ServingTierInfoV1* tiers = nullptr;
  uint32_t tierCount = 0;

  bool IsValid() const
  {
    return termInfo != nullptr && tiers != nullptr;
  }
};

class ServingPostingBlockCursor
{
public:
  ServingPostingBlockCursor() = default;

  bool IsValid() const;
  bool Next(ServingDecodedPosting* out);

private:
  friend class ServingShardSource;

  void Reset(const uint8_t* payload, uint64_t payloadLength, uint32_t docCount, uint32_t tierId);

  const uint8_t* cursor_ = nullptr;
  const uint8_t* end_ = nullptr;
  uint32_t remaining_ = 0;
  uint32_t decoded_ = 0;
  uint32_t previousDocId_ = 0;
  uint32_t tierId_ = 0;
  bool valid_ = false;
};

class ServingShardSource
{
public:
  ServingShardSource() = default;
  explicit ServingShardSource(const string& path);
  ~ServingShardSource();

  ServingShardSource(const ServingShardSource&) = delete;
  ServingShardSource& operator=(const ServingShardSource&) = delete;

  bool Load(const string& path);
  void Reset();
  bool IsLoaded() const;
  string GetError() const;

  size_t DocumentCount() const;
  size_t TermCount() const;
  bool LookupTerm(const string& term, ServingTermView* out) const;
  const ServingBlockInfoV1* GetBlockInfo(const ServingTierInfoV1& tier,
                                         uint32_t blockOffset) const;
  bool OpenBlockCursor(const ServingBlockInfoV1& block, uint32_t tierId,
                       ServingPostingBlockCursor* out) const;
  bool DecodeTermPostings(const string& term, vector<ServingDecodedPosting>* out) const;
  uint32_t GetDocLength(uint32_t docId) const;
  string GetDocumentURL(uint32_t docId) const;
  string GetDocumentTitle(uint32_t docId) const;
  string GetDocumentDescription(uint32_t docId) const;

private:
  bool Validate();
  const ServingLexiconEntryV1* FindLexiconEntry(const string& term) const;
  string ReadColdString(uint32_t offset, uint32_t length) const;
  void SetError(const char* message);
  void SetErrnoError(const char* operation, const string& path);

  string path_;
  string error_;
  vector<uint8_t> storage_;
  const uint8_t* data_ = nullptr;
  size_t fileSize_ = 0;
  bool loaded_ = false;

  const ServingShardHeaderV1* header_ = nullptr;
  const ServingLexiconEntryV1* lexicon_ = nullptr;
  const uint8_t* termBytes_ = nullptr;
  const ServingTermInfoV1* termInfos_ = nullptr;
  const ServingTierInfoV1* tierInfos_ = nullptr;
  const ServingBlockInfoV1* blockInfos_ = nullptr;
  const uint8_t* postings_ = nullptr;
  const uint32_t* docLengths_ = nullptr;
  const ServingDocMetadataEntryV1* coldDocs_ = nullptr;
};

} // namespace search
} // namespace avengine
