#include <avengine/codec.h>

#include <cassert>
#include <cstdint>
#include <cstring>

namespace avengine
{

void Writer::writeU64(uint64_t value)
{
  // Explicit byte-by-byte LE encoding — correct regardless of host endianness.
  for (int i = 0; i < 8; ++i)
    buf_.pushBack(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
}

void Writer::writeVarU32(uint32_t value)
{
  // Unsigned LEB128: emit 7 bits at a time, MSB of each byte signals
  // "more bytes follow".
  while (value >= 0x80u)
  {
    buf_.pushBack(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  buf_.pushBack(static_cast<uint8_t>(value));
}

void Writer::writeString(const string& str)
{
  writeU64(str.size());
  writeBytes(str.begin(), str.size());
}

void Writer::writeBytes(const void* data, size_t n)
{
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i)
    buf_.pushBack(p[i]);
}

const uint8_t* Writer::data() const
{
  return buf_.begin();
}

size_t Writer::size() const
{
  return buf_.size();
}

vector<uint8_t> Writer::release()
{
  // Move-construct a new vector from buf_, leaving buf_ empty.
  vector<uint8_t> result(static_cast<vector<uint8_t>&&>(buf_));
  return result;
}

Reader::Reader(const uint8_t* data, size_t size) : cursor_(data), end_(data + size) {}

size_t Reader::remaining() const
{
  return static_cast<size_t>(end_ - cursor_);
}

bool Reader::empty() const
{
  return cursor_ >= end_;
}

const uint8_t* Reader::cursor() const
{
  return cursor_;
}

uint64_t Reader::readU64()
{
  assert(remaining() >= 8 && "readU64: not enough bytes");

  uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(cursor_[i]) << (i * 8);

  cursor_ += 8;
  return value;
}

uint32_t Reader::readVarU32()
{
  uint32_t result = 0;
  uint32_t shift = 0;

  while (cursor_ < end_ && shift < 35)
  {
    const uint8_t byte = *cursor_++;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u)
      return result;
    shift += 7;
  }

  assert(false && "readVarU32: truncated or overlong varint (>5 bytes)");
  return 0;
}

string Reader::readString()
{
  if (remaining() < 8)
  {
    cursor_ = end_;
    return string();
  }
  const uint64_t len = readU64();
  if (len > remaining())
  {
    cursor_ = end_;
    return string();
  }
  string out;
  for (uint64_t i = 0; i < len; ++i)
    out.pushBack(static_cast<char>(cursor_[i]));
  cursor_ += len;
  return out;
}

void Reader::readBytes(void* out, size_t n)
{
  assert(remaining() >= n && "readBytes: not enough bytes");

  std::memcpy(out, cursor_, n);
  cursor_ += n;
}

void Reader::skip(size_t n)
{
  assert(remaining() >= n && "skip: not enough bytes");

  cursor_ += n;
}

} // namespace avengine
