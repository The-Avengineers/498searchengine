#include "avengine/index_stream_reader.h"

namespace avengine
{
namespace
{
static uint64_t read_u64_le(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; i++)
    value |= static_cast<uint64_t>(ptr[i]) << (i * 8);
  return value;
}

static uint32_t read_var_u32(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  uint32_t result = 0;
  uint32_t shift = 0;

  while (begin < end && shift < 35)
  {
    const uint8_t byte = *begin++;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u)
    {
      *next = begin;
      return result;
    }
    shift += 7;
  }

  *next = nullptr;
  return 0;
}

static bool skip_bytes(const uint8_t** cursor, const uint8_t* end, uint32_t count)
{
  if (count > static_cast<uint32_t>(end - *cursor))
    return false;

  *cursor += count;
  return true;
}
} // namespace

bool parsePostingBlobHeader(const uint8_t* begin, const uint8_t* end, PostingBlobHeader* out,
                            const uint8_t** fieldStreamsBegin)
{
  if (!begin || !end || !out || !fieldStreamsBegin)
    return false;
  if (begin + 56 > end)
    return false;

  out->documentFrequency = read_u64_le(begin);
  out->normalCollectionFrequency = read_u64_le(begin + 8);
  out->titleCollectionFrequency = read_u64_le(begin + 16);
  out->anchorCollectionFrequency = read_u64_le(begin + 24);
  out->normalSectionCount = read_u64_le(begin + 32);
  out->titleSectionCount = read_u64_le(begin + 40);
  out->anchorSectionCount = read_u64_le(begin + 48);
  *fieldStreamsBegin = begin + 56;
  return true;
}

bool IndexFieldStreamReader::init(const uint8_t* begin, const uint8_t* end, uint64_t sectionCount,
                                  uint64_t positionCount, FieldStreamEncoding encoding,
                                  IndexFieldStreamReader* out)
{
  if (!begin || !end || !out || begin > end)
    return false;

  out->cursor_ = begin;
  out->end_ = end;
  out->remainingPositions_ = positionCount;
  out->currentPosition_ = 0;
  out->decorators_.clear();

  const uint8_t* cursor = begin;
  const uint8_t* next = nullptr;

  if (encoding == FieldStreamEncoding::V2_SKIP_POINTERS)
  {
    for (uint64_t i = 0; i < sectionCount; i++)
    {
      (void)read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      (void)read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      (void)read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;
    }
  }
  else
  {
    for (uint64_t i = 0; i < sectionCount; i++)
    {
      const uint32_t type = read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      const uint32_t payloadBytes = read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      if (!skip_bytes(&cursor, end, payloadBytes))
        return false;

      out->decorators_.pushBack({type, payloadBytes});
    }
  }

  out->cursor_ = cursor;
  return true;
}

bool IndexFieldStreamReader::nextPosition(uint32_t* absolutePosition)
{
  if (!absolutePosition || remainingPositions_ == 0)
    return false;

  const uint8_t* next = nullptr;
  const uint32_t delta = read_var_u32(cursor_, end_, &next);
  if (!next)
    return false;

  cursor_ = next;
  currentPosition_ += delta;
  remainingPositions_--;
  *absolutePosition = currentPosition_;
  return true;
}

const uint8_t* IndexFieldStreamReader::nextCursor() const
{
  return cursor_;
}

uint64_t IndexFieldStreamReader::remainingPositions() const
{
  return remainingPositions_;
}

const avengine::vector<FieldDecorator>& IndexFieldStreamReader::decorators() const
{
  return decorators_;
}

} // namespace avengine
