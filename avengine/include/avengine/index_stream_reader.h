#pragma once

#include "avengine/vector.h"
#include <cstddef>
#include <cstdint>

namespace avengine
{

enum class FieldStreamEncoding
{
  V2_SKIP_POINTERS,
  V3_DECORATORS,
};

struct FieldDecorator
{
  uint32_t type = 0;
  uint32_t payloadBytes = 0;
};

struct PostingBlobHeader
{
  uint64_t documentFrequency = 0;
  uint64_t normalCollectionFrequency = 0;
  uint64_t titleCollectionFrequency = 0;
  uint64_t anchorCollectionFrequency = 0;
  uint64_t normalSectionCount = 0;
  uint64_t titleSectionCount = 0;
  uint64_t anchorSectionCount = 0;
};

// Incremental reader for one posting field stream. This is intended as a stable
// ISR primitive for future constraint-solver iterators.
class IndexFieldStreamReader
{
public:
  static bool init(const uint8_t* begin, const uint8_t* end, uint64_t sectionCount,
                   uint64_t positionCount, FieldStreamEncoding encoding, IndexFieldStreamReader* out);

  bool nextPosition(uint32_t* absolutePosition);
  const uint8_t* nextCursor() const;
  uint64_t remainingPositions() const;
  const avengine::vector<FieldDecorator>& decorators() const;

private:
  const uint8_t* cursor_ = nullptr;
  const uint8_t* end_ = nullptr;
  uint64_t remainingPositions_ = 0;
  uint32_t currentPosition_ = 0;
  avengine::vector<FieldDecorator> decorators_;
};

bool parsePostingBlobHeader(const uint8_t* begin, const uint8_t* end, PostingBlobHeader* out,
                            const uint8_t** fieldStreamsBegin);

} // namespace avengine

