#pragma once

#include <avengine/avengine_lib/vector.h>
#include <avengine/crawler/frontier.h>

namespace avengine
{
namespace crawler
{

// Push a single entry onto the shared inbound URL queue.
// Thread-safe: may be called from any thread (e.g., a peerRecvThread).
void pushInbound(const FullURLEntry& entry);

// Drain all pending entries from the inbound queue into `out`.
// Swaps the internal queue with `out` while holding the lock, then returns.
// `out` is cleared before receiving the drained entries.
// Thread-safe: may be called from the main crawl loop.
void drainInbound(vector<FullURLEntry>& out);

} // namespace crawler
} // namespace avengine
