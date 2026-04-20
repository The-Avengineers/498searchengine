#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

#include <avengine/search/serving_index_format.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

using avengine::search::ServingBlockInfoV1;
using avengine::search::ServingDocMetadataView;
using avengine::search::ServingDocMetadataEntryV1;
using avengine::search::ServingDocPositionEntryV1;
using avengine::search::ServingDocRankMetadataV1;
using avengine::search::ServingLexiconEntryV1;
using avengine::search::ServingPositionSectionHeaderV1;
using avengine::search::ServingShardHeaderV1;
using avengine::search::ServingTermInfoV1;
using avengine::search::ServingTermPositionInfoV1;
using avengine::search::ServingTierId;
using avengine::search::ServingTierInfoV1;

TEST_CASE("serving index wire records keep fixed layout")
{
  CHECK(std::is_standard_layout<ServingShardHeaderV1>::value);
  CHECK(std::is_trivially_copyable<ServingShardHeaderV1>::value);
  CHECK(std::is_standard_layout<ServingLexiconEntryV1>::value);
  CHECK(std::is_trivially_copyable<ServingLexiconEntryV1>::value);
  CHECK(std::is_standard_layout<ServingTermInfoV1>::value);
  CHECK(std::is_trivially_copyable<ServingTermInfoV1>::value);
  CHECK(std::is_standard_layout<ServingTierInfoV1>::value);
  CHECK(std::is_trivially_copyable<ServingTierInfoV1>::value);
  CHECK(std::is_standard_layout<ServingBlockInfoV1>::value);
  CHECK(std::is_trivially_copyable<ServingBlockInfoV1>::value);
  CHECK(std::is_standard_layout<ServingDocMetadataEntryV1>::value);
  CHECK(std::is_trivially_copyable<ServingDocMetadataEntryV1>::value);
  CHECK(std::is_standard_layout<ServingDocRankMetadataV1>::value);
  CHECK(std::is_trivially_copyable<ServingDocRankMetadataV1>::value);
  CHECK(std::is_standard_layout<ServingPositionSectionHeaderV1>::value);
  CHECK(std::is_trivially_copyable<ServingPositionSectionHeaderV1>::value);
  CHECK(std::is_standard_layout<ServingTermPositionInfoV1>::value);
  CHECK(std::is_trivially_copyable<ServingTermPositionInfoV1>::value);
  CHECK(std::is_standard_layout<ServingDocPositionEntryV1>::value);
  CHECK(std::is_trivially_copyable<ServingDocPositionEntryV1>::value);

  CHECK(sizeof(ServingShardHeaderV1) == 256u);
  CHECK(sizeof(ServingLexiconEntryV1) == 24u);
  CHECK(sizeof(ServingTermInfoV1) == 40u);
  CHECK(sizeof(ServingTierInfoV1) == 40u);
  CHECK(sizeof(ServingBlockInfoV1) == 48u);
  CHECK(sizeof(ServingDocMetadataEntryV1) == 24u);
  CHECK(sizeof(ServingDocRankMetadataV1) == 16u);
  CHECK(sizeof(ServingPositionSectionHeaderV1) == 72u);
  CHECK(sizeof(ServingTermPositionInfoV1) == 8u);
  CHECK(sizeof(ServingDocPositionEntryV1) == 24u);
}

TEST_CASE("serving index header offsets are stable")
{
  CHECK(offsetof(ServingShardHeaderV1, magic) == 0u);
  CHECK(offsetof(ServingShardHeaderV1, version) == 8u);
  CHECK(offsetof(ServingShardHeaderV1, flags) == 16u);
  CHECK(offsetof(ServingShardHeaderV1, shardId) == 24u);
  CHECK(offsetof(ServingShardHeaderV1, lexiconOffset) == 72u);
  CHECK(offsetof(ServingShardHeaderV1, termInfoOffset) == 104u);
  CHECK(offsetof(ServingShardHeaderV1, tierInfoOffset) == 120u);
  CHECK(offsetof(ServingShardHeaderV1, blockInfoOffset) == 136u);
  CHECK(offsetof(ServingShardHeaderV1, postingsOffset) == 152u);
  CHECK(offsetof(ServingShardHeaderV1, docLengthOffset) == 168u);
  CHECK(offsetof(ServingShardHeaderV1, coldPayloadOffset) == 200u);
  CHECK(offsetof(ServingShardHeaderV1, contentChecksum) == 232u);
}

TEST_CASE("serving index metadata offsets are stable")
{
  CHECK(offsetof(ServingLexiconEntryV1, termOffset) == 0u);
  CHECK(offsetof(ServingLexiconEntryV1, termInfoIndex) == 12u);
  CHECK(offsetof(ServingLexiconEntryV1, termHash) == 16u);

  CHECK(offsetof(ServingTermInfoV1, collectionFrequency) == 0u);
  CHECK(offsetof(ServingTermInfoV1, tierInfoIndex) == 12u);
  CHECK(offsetof(ServingTermInfoV1, maxScoreQ16) == 20u);
  CHECK(offsetof(ServingTermInfoV1, bodyDocumentFrequency) == 24u);
  CHECK(offsetof(ServingTermInfoV1, anchorDocumentFrequency) == 32u);

  CHECK(offsetof(ServingTierInfoV1, tierId) == 0u);
  CHECK(offsetof(ServingTierInfoV1, blockIndex) == 8u);
  CHECK(offsetof(ServingTierInfoV1, payloadOffset) == 16u);
  CHECK(offsetof(ServingTierInfoV1, maxScoreQ16) == 32u);

  CHECK(offsetof(ServingBlockInfoV1, firstDocId) == 0u);
  CHECK(offsetof(ServingBlockInfoV1, maxScoreQ16) == 12u);
  CHECK(offsetof(ServingBlockInfoV1, payloadOffset) == 16u);
  CHECK(offsetof(ServingBlockInfoV1, maxBodyTf) == 32u);
  CHECK(offsetof(ServingBlockInfoV1, fieldMaskUnion) == 44u);

  CHECK(offsetof(ServingDocMetadataEntryV1, urlOffset) == 0u);
  CHECK(offsetof(ServingDocMetadataEntryV1, titleOffset) == 8u);
  CHECK(offsetof(ServingDocMetadataEntryV1, descriptionOffset) == 16u);

  CHECK(offsetof(ServingDocRankMetadataV1, depth) == 4u);
  CHECK(offsetof(ServingDocRankMetadataV1, titleWordCount) == 10u);
  CHECK(offsetof(ServingPositionSectionHeaderV1, termPositionOffset) == 24u);
  CHECK(offsetof(ServingPositionSectionHeaderV1, payloadOffset) == 56u);
  CHECK(offsetof(ServingDocPositionEntryV1, fieldId) == 4u);
  CHECK(offsetof(ServingDocPositionEntryV1, payloadOffset) == 12u);
}

TEST_CASE("empty serving shard header is self-consistent")
{
  const ServingShardHeaderV1 header = avengine::search::MakeEmptyServingShardHeaderV1(42u);

  CHECK(avengine::search::IsServingShardMagic(header.magic));
  CHECK(avengine::search::IsSupportedServingShardVersion(header.version));
  CHECK(header.headerBytes == sizeof(ServingShardHeaderV1));
  CHECK(header.blockSize == avengine::search::kServingDefaultBlockSize);
  CHECK(header.shardId == 42u);
  CHECK(header.documentCount == 0u);
  CHECK(header.lexiconEntryCount == 0u);
  CHECK(header.termCount == 0u);
  CHECK(header.tierCount == 0u);
  CHECK(header.blockCount == 0u);

  CHECK(header.lexiconOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.termBytesOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.termInfoOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.tierInfoOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.blockInfoOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.postingsOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.docLengthOffset == sizeof(ServingShardHeaderV1));
  CHECK(header.coldPayloadOffset == sizeof(ServingShardHeaderV1));

  CHECK(header.lexiconLength == 0u);
  CHECK(header.termBytesLength == 0u);
  CHECK(header.postingsLength == 0u);
  CHECK(header.coldPayloadLength == 0u);
}

TEST_CASE("serving tiers and field masks are stable")
{
  CHECK(static_cast<uint32_t>(ServingTierId::Strong) == 0u);
  CHECK(static_cast<uint32_t>(ServingTierId::Medium) == 1u);
  CHECK(static_cast<uint32_t>(ServingTierId::Recall) == 2u);
  CHECK(avengine::search::kServingTierCount == 3u);
  CHECK(avengine::search::IsValidServingTierId(0u));
  CHECK(avengine::search::IsValidServingTierId(1u));
  CHECK(avengine::search::IsValidServingTierId(2u));
  CHECK(!avengine::search::IsValidServingTierId(3u));

  CHECK(avengine::search::kServingFieldBody == 1u);
  CHECK(avengine::search::kServingFieldTitle == 2u);
  CHECK(avengine::search::kServingFieldAnchor == 4u);
}

TEST_CASE("doc metadata view uses serving doc ids")
{
  const uint32_t lengths[] = {10u, 20u, 30u};
  const uint32_t ranks[] = {100u, 80u, 60u};
  ServingDocMetadataView view = {.docLengths = lengths, .staticRanks = ranks, .documentCount = 3u};

  uint32_t value = 0;
  REQUIRE(view.GetDocLength(1u, &value));
  CHECK(value == 20u);
  REQUIRE(view.GetStaticRank(2u, &value));
  CHECK(value == 60u);

  CHECK(!view.GetDocLength(3u, &value));
  CHECK(!view.GetStaticRank(3u, &value));
  CHECK(!view.GetDocLength(0u, nullptr));

  ServingDocMetadataView noRanks = {.docLengths = lengths, .staticRanks = nullptr,
                                    .documentCount = 3u};
  CHECK(noRanks.GetDocLength(0u, &value));
  CHECK(value == 10u);
  CHECK(!noRanks.GetStaticRank(0u, &value));
}
