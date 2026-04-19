#pragma once

#include <cstddef>
#include <cstdint>

namespace avengine
{
namespace search
{

// Serving shard v1 is the rebuilt deployment format consumed by search.
//
// Layout:
//   [ServingShardHeaderV1]
//   [ServingLexiconEntryV1 ...]
//   [term bytes blob]
//   [ServingTermInfoV1 ...]
//   [ServingTierInfoV1 ...]
//   [ServingBlockInfoV1 ...]
//   [hot postings payload]
//   [hot doc length array: u32[documentCount]]
//   [optional static rank array: u32[documentCount]]
//   [optional cold payload]
//   [optional debug payload]
//
// All offsets in the header are absolute byte offsets from the start of the
// file. Offsets inside lexicon entries are relative to termBytesOffset. Offsets
// inside tier and block metadata are relative to postingsOffset. Multi-byte
// integer fields are stored little-endian on disk.

constexpr uint64_t kServingShardMagic = 0x3130304953535641ULL; // "AVSSI001" as LE bytes.
constexpr uint32_t kServingShardVersion = 1;
constexpr uint32_t kServingDefaultBlockSize = 128;
constexpr uint32_t kServingTierCount = 3;

enum ServingShardFlags : uint32_t
{
  kServingShardHasColdPayload = 1u << 0u,
  kServingShardHasDebugSection = 1u << 1u,
  kServingShardHasStaticRank = 1u << 2u,
};

enum class ServingTierId : uint32_t
{
  Strong = 0, // title or anchor evidence
  Medium = 1, // stronger body-only evidence
  Recall = 2, // remaining body-only evidence
};

enum ServingFieldMask : uint8_t
{
  kServingFieldBody = 1u << 0u,
  kServingFieldTitle = 1u << 1u,
  kServingFieldAnchor = 1u << 2u,
};

// Header is intentionally fixed-size so loaders can validate a shard before
// reading any variable-length section.
struct ServingShardHeaderV1
{
  uint64_t magic = 0;
  uint32_t version = 0;
  uint32_t headerBytes = 0;
  uint32_t flags = 0;
  uint32_t blockSize = 0;

  uint64_t shardId = 0;
  uint64_t documentCount = 0;
  uint64_t lexiconEntryCount = 0;
  uint64_t termCount = 0;
  uint64_t tierCount = 0;
  uint64_t blockCount = 0;

  uint64_t lexiconOffset = 0;
  uint64_t lexiconLength = 0;
  uint64_t termBytesOffset = 0;
  uint64_t termBytesLength = 0;
  uint64_t termInfoOffset = 0;
  uint64_t termInfoLength = 0;
  uint64_t tierInfoOffset = 0;
  uint64_t tierInfoLength = 0;
  uint64_t blockInfoOffset = 0;
  uint64_t blockInfoLength = 0;
  uint64_t postingsOffset = 0;
  uint64_t postingsLength = 0;
  uint64_t docLengthOffset = 0;
  uint64_t docLengthLength = 0;
  uint64_t staticRankOffset = 0;
  uint64_t staticRankLength = 0;
  uint64_t coldPayloadOffset = 0;
  uint64_t coldPayloadLength = 0;
  uint64_t debugOffset = 0;
  uint64_t debugLength = 0;

  uint64_t contentChecksum = 0;
  uint64_t reserved0 = 0;
  uint64_t reserved1 = 0;
};

// One sorted lexicon entry per normalized term. termInfoIndex points into the
// term metadata table; v1 keeps exactly one ServingTermInfoV1 per lexicon term.
struct ServingLexiconEntryV1
{
  uint64_t termOffset = 0;
  uint32_t termLength = 0;
  uint32_t termInfoIndex = 0;
  uint64_t termHash = 0; // fast negative filter only; bytes remain authoritative.
};

// Per-term jump point into tier metadata. Field document frequencies are kept
// hot so first-stage scoring and diagnostics do not need to scan postings.
struct ServingTermInfoV1
{
  uint64_t collectionFrequency = 0;
  uint32_t documentFrequency = 0;
  uint32_t tierInfoIndex = 0;
  uint32_t tierCount = 0;
  uint32_t maxScoreQ16 = 0;
  uint32_t bodyDocumentFrequency = 0;
  uint32_t titleDocumentFrequency = 0;
  uint32_t anchorDocumentFrequency = 0;
  uint32_t reserved = 0;
};

// One record per non-empty term/tier pair. payloadOffset/payloadLength bound all
// blocks in this tier, while blockIndex/blockCount let the query engine scan
// compact block metadata before decoding payload bytes.
struct ServingTierInfoV1
{
  uint32_t tierId = 0;
  uint32_t postingCount = 0;
  uint32_t blockIndex = 0;
  uint32_t blockCount = 0;
  uint64_t payloadOffset = 0;
  uint64_t payloadLength = 0;
  uint32_t maxScoreQ16 = 0;
  uint32_t reserved = 0;
};

// Fixed-size block-max metadata. Payload offsets are relative to the hot
// postings payload section. firstDocId/lastDocId describe decoded docIDs after
// applying block-local deltas.
struct ServingBlockInfoV1
{
  uint32_t firstDocId = 0;
  uint32_t lastDocId = 0;
  uint32_t docCount = 0;
  uint32_t maxScoreQ16 = 0;
  uint64_t payloadOffset = 0;
  uint64_t payloadLength = 0;
  uint32_t maxBodyTf = 0;
  uint32_t maxTitleTf = 0;
  uint32_t maxAnchorTf = 0;
  uint32_t fieldMaskUnion = 0;
};

// Non-owning view over hot per-document metadata arrays. These arrays are
// indexed by serving docID, not by source-shard docID.
struct ServingDocMetadataView
{
  const uint32_t* docLengths = nullptr;
  const uint32_t* staticRanks = nullptr;
  uint64_t documentCount = 0;

  bool GetDocLength(uint32_t docId, uint32_t* out) const;
  bool GetStaticRank(uint32_t docId, uint32_t* out) const;
};

ServingShardHeaderV1 MakeEmptyServingShardHeaderV1(uint64_t shardId = 0);
bool IsServingShardMagic(uint64_t magic);
bool IsSupportedServingShardVersion(uint32_t version);
bool IsValidServingTierId(uint32_t tierId);

static_assert(sizeof(ServingShardHeaderV1) == 256, "ServingShardHeaderV1 wire size changed");
static_assert(sizeof(ServingLexiconEntryV1) == 24, "ServingLexiconEntryV1 wire size changed");
static_assert(sizeof(ServingTermInfoV1) == 40, "ServingTermInfoV1 wire size changed");
static_assert(sizeof(ServingTierInfoV1) == 40, "ServingTierInfoV1 wire size changed");
static_assert(sizeof(ServingBlockInfoV1) == 48, "ServingBlockInfoV1 wire size changed");

} // namespace search
} // namespace avengine
