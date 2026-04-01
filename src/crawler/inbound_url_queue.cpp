#include <avengine/avengine_lib/lock_guard.h>
#include <avengine/avengine_lib/mutex.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/crawler/frontier.h>
#include <avengine/crawler/inbound_url_queue.h>

namespace
{
static avengine::mutex gInboundMu;
static avengine::vector<avengine::crawler::FullURLEntry> gInboundQueue;
} // namespace

namespace avengine
{
namespace crawler
{

void pushInbound(const FullURLEntry& entry)
{
  lock_guard<mutex> guard(gInboundMu);
  gInboundQueue.pushBack(entry);
}

void drainInbound(vector<FullURLEntry>& out)
{
  out.clear();
  lock_guard<mutex> guard(gInboundMu);
  gInboundQueue.swap(out);
}

} // namespace crawler
} // namespace avengine
