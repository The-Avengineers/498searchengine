#include <avengine/search/serving_index_format.h>

namespace avengine
{
namespace search
{

bool ServingDocMetadataView::GetDocLength(uint32_t docId, uint32_t* out) const
{
  if (!out || !docLengths || static_cast<uint64_t>(docId) >= documentCount)
    return false;

  *out = docLengths[docId];
  return true;
}

bool ServingDocMetadataView::GetStaticRank(uint32_t docId, uint32_t* out) const
{
  if (!out || !staticRanks || static_cast<uint64_t>(docId) >= documentCount)
    return false;

  *out = staticRanks[docId];
  return true;
}

ServingShardHeaderV1 MakeEmptyServingShardHeaderV1(uint64_t shardId)
{
  ServingShardHeaderV1 header = {};
  header.magic = kServingShardMagic;
  header.version = kServingShardVersion;
  header.headerBytes = static_cast<uint32_t>(sizeof(ServingShardHeaderV1));
  header.blockSize = kServingDefaultBlockSize;
  header.shardId = shardId;

  const uint64_t emptyOffset = sizeof(ServingShardHeaderV1);
  header.lexiconOffset = emptyOffset;
  header.termBytesOffset = emptyOffset;
  header.termInfoOffset = emptyOffset;
  header.tierInfoOffset = emptyOffset;
  header.blockInfoOffset = emptyOffset;
  header.postingsOffset = emptyOffset;
  header.docLengthOffset = emptyOffset;
  header.staticRankOffset = emptyOffset;
  header.coldPayloadOffset = emptyOffset;
  header.debugOffset = emptyOffset;
  return header;
}

bool IsServingShardMagic(uint64_t magic)
{
  return magic == kServingShardMagic;
}

bool IsSupportedServingShardVersion(uint32_t version)
{
  return version >= kServingShardMinReadableVersion && version <= kServingShardVersion;
}

bool IsValidServingTierId(uint32_t tierId)
{
  return tierId < kServingTierCount;
}

} // namespace search
} // namespace avengine
