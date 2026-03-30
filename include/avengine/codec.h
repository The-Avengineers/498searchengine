#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/vector.h>
#include <cstddef>
#include <cstdint>

namespace avengine
{

// Accumulates a sequence of typed values into an internal byte buffer.
// Write methods never fail: the buffer grows as needed via pushBack.
//
// After writing, hand off the result with data()/size() or release():
class Writer
{
public:
  Writer() = default;

  // Append a fixed 8-byte little-endian uint64.
  void writeU64(uint64_t value);

  // Append a variable-length LEB128-encoded uint32 (1–5 bytes).
  void writeVarU32(uint32_t value);

  // Append a length-prefixed string: writeU64(len) followed by len
  // raw bytes. No null terminator is written.
  void writeString(const string& str);

  // Append n raw bytes from data. Prefer the typed methods above;
  // use this for fixed-size POD fields (e.g. structs, floats).
  void writeBytes(const void* data, size_t n);

  // Read-only view of the bytes accumulated so far.
  const uint8_t* data() const;
  size_t size() const;

  // Transfer ownership of the internal buffer, leaving the Writer empty.
  // O(1) — no copy.
  vector<uint8_t> release();

private:
  vector<uint8_t> buf_;
};

// Reads sequentially from a contiguous byte range it does NOT own.
// The backing memory must outlive the Reader.
//
// Every read method has a strong rollback guarantee: if it returns an
// error, the internal cursor is left exactly where it was before the
// call. Successful reads advance the cursor past the consumed bytes.
class Reader
{
public:
  // Wrap the byte range [data, data + size). Does not copy.
  Reader(const uint8_t* data, size_t size);

  // Number of bytes not yet consumed.
  size_t remaining() const;

  // True when all bytes have been consumed.
  bool empty() const;

  // Pointer to the next byte that will be read.
  // Useful for computing relative byte offsets (e.g. skip-pointer tables).
  const uint8_t* cursor() const;

  // Read a fixed 8-byte little-endian uint64.
  // Asserts that at least 8 bytes remain.
  uint64_t readU64();

  // Read a variable-length LEB128 uint32 (1–5 bytes).
  // Asserts that the stream is not truncated before the terminating byte and
  // that the encoded value fits in 32 bits (no more than 5 continuation bytes).
  uint32_t readVarU32();

  // Read a varint-length-prefixed string (as written by Writer::writeString).
  // Asserts that the length varint and body bytes are not truncated.
  string readString();

  // Copy exactly n raw bytes from the current position into out[0..n).
  // out must point to at least n writable bytes.
  // Asserts that at least n bytes remain.
  void readBytes(void* out, size_t n);

  // Advance the cursor by n bytes without copying them.
  // Asserts that at least n bytes remain.
  void skip(size_t n);

private:
  const uint8_t* cursor_;
  const uint8_t* end_;
};

} // namespace avengine
