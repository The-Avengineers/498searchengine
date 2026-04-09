#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/codec.h>
#include <avengine/crawler/frontier.h>
#include <cstdint>

namespace avengine
{
namespace crawler
{

// Encode a batch of urlEntryWithAnchor entries into a byte buffer.
inline vector<uint8_t> encodeFrontierBatch(const vector<FullURLEntry>& entries)
{
  Writer w;
  w.writeU64(entries.size());
  for (size_t i = 0; i < entries.size(); ++i)
  {
    const FullURLEntry& e = entries[i];
    w.writeString(e.entry.url);
    w.writeString(e.anchorText);
    w.writeU64(static_cast<uint64_t>(e.entry.depth));
    w.writeU64(static_cast<uint64_t>(e.entry.seedDomainDepth));
    w.writeU64(static_cast<uint64_t>(e.entry.seedSourceLine));
  }
  return w.release();
}

// Decode a byte buffer produced by encodeFrontierBatch into entries.
// Returns false if the buffer is malformed or truncated.
inline bool decodeFrontierBatch(const vector<uint8_t>& buf, vector<FullURLEntry>& out)
{
  Reader r(buf.begin(), buf.size());
  uint64_t count = r.readU64();

  for (uint64_t i = 0; i < count; ++i)
  {
    // Read URL
    if (r.remaining() < 8)
    {
      return false;
    }

    string url = r.readString();
    string anchorText = r.readString();

    if (r.remaining() < 24)
    {
      return false;
    }

    uint64_t depth = r.readU64();
    uint64_t seedDomainDepth = r.readU64();
    uint64_t seedSourceLine = r.readU64();

    FullURLEntry entry;
    entry.entry.url = url;
    entry.entry.depth = static_cast<int>(depth);
    entry.entry.seedDomainDepth = static_cast<int>(seedDomainDepth);
    entry.entry.seedSourceLine = static_cast<int>(seedSourceLine);
    entry.anchorText = anchorText;
    out.pushBack(entry);
  }

  return true;
}

} // namespace crawler
} // namespace avengine
