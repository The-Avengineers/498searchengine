#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/index_stream_reader.h>

namespace
{
void appendVarU32(avengine::vector<uint8_t>& out, uint32_t value)
{
  while (value >= 0x80u)
  {
    out.pushBack(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  out.pushBack(static_cast<uint8_t>(value));
}

void appendU64LE(avengine::vector<uint8_t>& out, uint64_t value)
{
  for (int i = 0; i < 8; i++)
    out.pushBack(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
}
} // namespace

TEST_CASE("parsePostingBlobHeader parses 56-byte header")
{
  avengine::vector<uint8_t> bytes;
  appendU64LE(bytes, 11u);
  appendU64LE(bytes, 12u);
  appendU64LE(bytes, 13u);
  appendU64LE(bytes, 14u);
  appendU64LE(bytes, 15u);
  appendU64LE(bytes, 16u);
  appendU64LE(bytes, 17u);

  avengine::PostingBlobHeader header = {};
  const uint8_t* fields = nullptr;
  REQUIRE(avengine::parsePostingBlobHeader(bytes.begin(), bytes.end(), &header, &fields));
  CHECK(header.documentFrequency == 11u);
  CHECK(header.normalCollectionFrequency == 12u);
  CHECK(header.titleCollectionFrequency == 13u);
  CHECK(header.anchorCollectionFrequency == 14u);
  CHECK(header.normalSectionCount == 15u);
  CHECK(header.titleSectionCount == 16u);
  CHECK(header.anchorSectionCount == 17u);
  CHECK(fields == bytes.begin() + 56);
}

TEST_CASE("IndexFieldStreamReader decodes v2 skip sections and position deltas")
{
  avengine::vector<uint8_t> stream;

  // one v2 section: [position_index=2][abs_pos=7][byte_offset=3]
  appendVarU32(stream, 2u);
  appendVarU32(stream, 7u);
  appendVarU32(stream, 3u);

  // positions: 4, 10, 15
  appendVarU32(stream, 4u);
  appendVarU32(stream, 6u);
  appendVarU32(stream, 5u);

  avengine::IndexFieldStreamReader reader;
  REQUIRE(avengine::IndexFieldStreamReader::init(
      stream.begin(), stream.end(), 1u, 3u, avengine::FieldStreamEncoding::V2_SKIP_POINTERS,
      &reader));

  uint32_t position = 0;
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 4u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 10u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 15u);
  CHECK(reader.remainingPositions() == 0u);
  CHECK_FALSE(reader.nextPosition(&position));
}

TEST_CASE("IndexFieldStreamReader decodes v3 decorators and position deltas")
{
  avengine::vector<uint8_t> stream;

  // two v3 decorators:
  // [type=1][payload=2 bytes: AA BB], [type=3][payload=1 byte: 01]
  appendVarU32(stream, 1u);
  appendVarU32(stream, 2u);
  stream.pushBack(0xAAu);
  stream.pushBack(0xBBu);
  appendVarU32(stream, 3u);
  appendVarU32(stream, 1u);
  stream.pushBack(0x01u);

  // positions: 1, 4, 11
  appendVarU32(stream, 1u);
  appendVarU32(stream, 3u);
  appendVarU32(stream, 7u);

  avengine::IndexFieldStreamReader reader;
  REQUIRE(avengine::IndexFieldStreamReader::init(
      stream.begin(), stream.end(), 2u, 3u, avengine::FieldStreamEncoding::V3_DECORATORS,
      &reader));

  REQUIRE(reader.decorators().size() == 2u);
  CHECK(reader.decorators()[0].type == 1u);
  CHECK(reader.decorators()[0].payloadBytes == 2u);
  CHECK(reader.decorators()[1].type == 3u);
  CHECK(reader.decorators()[1].payloadBytes == 1u);

  uint32_t position = 0;
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 1u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 4u);
  REQUIRE(reader.nextPosition(&position));
  CHECK(position == 11u);
  CHECK(reader.remainingPositions() == 0u);
}

