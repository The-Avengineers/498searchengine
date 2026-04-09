#include "../../include/avengine/crawler/domain_filter.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/frontier.h"
#include "../../include/avengine/crawler/frontier_message.h"
#include "../../include/avengine/crawler/inbound_url_queue.h"
#include "../../include/avengine/crawler/logger.h"
#include "../../include/avengine/crawler/robots_helper.h"
#include "../../include/avengine/crawler/url_normalizer.h"
#include "../../include/avengine/html_parser/HtmlParser.h"
#include "avengine/avengine_lib/condition_variable.h"
#include "avengine/avengine_lib/deque.h"
#include "avengine/avengine_lib/mutex.h"
#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/string_slice.h"
#include "avengine/avengine_lib/thread.h"
#include <avengine/indexer/anchor_indexer.h>
#include <avengine/indexer/indexer.h>
#include <avengine/node.h>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <unistd.h>

using namespace avengine;
using namespace avengine::crawler;

// TODO THREAD POOL AROUND ALL THIS

// When crawling about 300 pages/s it checkpoints every 10 minutes
static const size_t CHECKPOINT_INTERVAL = 180000;

namespace
{

// For final checkpointing when we run control C.
volatile sig_atomic_t gStopRequested = 0;
const Utf8 kRobotsUserAgent[] = "Avengineers-Crawler";

void handleStopSignal(int)
{
  gStopRequested = 1;
}

bool stopRequested()
{
  return gStopRequested != 0;
}

// Ensure that the host is valid before we add to the frontier.
bool hasValidHost(URLNormalizer& normalizer, const string& url)
{
  URL parsed = normalizer.parseURL(url);
  return parsed.host.size() > 0;
}

// Returns true if the URL's path ends with a common non-HTML extension that we
// should not crawl (images, archives, executables, etc.).
bool hasBlockedExtension(URLNormalizer& normalizer, const string& url)
{
  URL parsed = normalizer.parseURL(url);
  const string& path = parsed.path;
  size_t pathLen = path.size();
  if (pathLen == 0)
    return false;

  // Walk backwards to find the last '.'
  size_t dotPos = string::npos;
  for (size_t i = pathLen; i-- > 0;)
  {
    if (path[i] == '.')
    {
      dotPos = i;
      break;
    }
    // Stop at a path separator — no extension in this segment
    if (path[i] == '/')
      break;
  }

  if (dotPos == string::npos)
    return false;

  // Build a lower-cased extension (without the dot)
  string ext;
  for (size_t i = dotPos + 1; i < pathLen; ++i)
  {
    char c = path[i];
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + ('a' - 'A'));
    ext.pushBack(c);
  }

  // Blocked extensions list
  const char* blocked[] = {"jpg", "jpeg", "png",  "gif", "svg",  "pdf",  "gz",  "zip",
                           "msi", "dmg",  "exe",  "xz",  "ogg",  "mp3",  "wav", "aac",
                           "mp4", "mkv",  "mov",  "dll", "so",   "ttf",  "otf", "woff",
                           "csv", "xls",  "xlsx", "ppt", "pptx", nullptr};
  for (size_t i = 0; blocked[i] != nullptr; ++i)
  {
    if (ext == blocked[i])
      return true;
  }
  return false;
}

// Making sure every URL contains "HTTPS" to maintain consistency
string https_url_helper(URLNormalizer& normalizer, const string& url)
{
  string normalized = normalizer.normalizeIndividual(url);
  if (normalized.size() == 0)
  {
    return string();
  }

  URL parsed = normalizer.parseURL(normalized);
  if (parsed.host.size() == 0)
  {
    return string();
  }

  parsed.protocol = "https";
  if (parsed.port == "80" || parsed.port == "443")
  {
    parsed.port = "";
  }
  return normalizer.URLtoString(parsed);
}

// Populates the seeds vector with URLs in the file
void fill_seeds(vector<string>& seeds, URLNormalizer& normalizer, const char* path)
{
  std::ifstream in(path);
  if (!in.is_open())
  {
    return;
  }

  char line[1024];
  while (in.getline(line, sizeof(line)))
  {
    if (line[0] == '\0' || line[0] == '#')
    {
      continue;
    }

    string normalized = https_url_helper(normalizer, line);
    if (normalized.size() == 0)
    {
      continue;
    }
    seeds.pushBack(normalized);
  }
}

bool responseContainsBlockedWords(const domainFilter& filter, const HtmlParser& parser)
{
  return filter.containsBlockedWords(parser.titleWords) ||
         filter.containsBlockedWords(parser.words);
}

bool anchorContainsBlockedWords(const domainFilter& filter, const Link& link)
{
  return filter.containsBlockedWords(link.anchorText);
}

vector<urlEntry> filterBlockedBatch(const vector<urlEntry>& batch, const domainFilter& filter,
                                    CrawlLogger& crawlLogger)
{
  vector<urlEntry> allowedBatch;
  for (size_t i = 0; i < batch.size(); ++i)
  {
    if (filter.isBlocked(batch[i].url))
    {
      crawlLogger.log(batch[i].url, "SKIP, BLOCKED URL");
      continue;
    }
    allowedBatch.pushBack(batch[i]);
  }
  return allowedBatch;
}

struct globalCrawlerState
{
  frontier* theFrontier;
  domainFilter* filter;
  CrawlLogger* crawlLogger;
  Robots_Agent* robotsAgent;
  mutex frontierMu;
  mutex queueMu;
  condition_variable queueCV;
  condition_variable batchDoneCV;
  deque<urlEntry> q;
  bool shuttingDown;
  atomic_counter* processedCount;
  atomic_counter* indexedCount;
  // Per-peer outbound URL buffers for distributed crawling.
  // Sized to numPeers during setup; entry [localNodeId] is never used.
  vector<vector<crawler::FullURLEntry>> outbound;
  mutex outboundMu;

  globalCrawlerState()
      : theFrontier(nullptr), filter(nullptr), crawlLogger(nullptr), robotsAgent(nullptr), q(),
        shuttingDown(false), processedCount(nullptr), indexedCount(nullptr)
  {
  }
};

struct threadArgs
{
  globalCrawlerState* state;
};

// Routes a normalized URL (in a multi-peer crawl) to the correct node. If this
// node owns the URL it is added directly to the local frontier; otherwise it
// is enqueued in the appropriate outbound buffer so that the network-dispatch
// thread can forward it to the owning peer.
void routeURLToPeer(const string& normalized, int depth,
                    const vector<string_slice>& anchorTextSlices, globalCrawlerState& state)
{
  size_t owner = hashString(normalized) % numPeers;
  if (owner == localNodeId)
  {
    state.frontierMu.lock();
    state.theFrontier->add(normalized, depth);
    state.frontierMu.unlock();
    saveAnchorText(normalized, anchorTextSlices);
  }
  else
  {
    // Join all anchor text slices into a single space-separated string.
    string anchorText;
    for (size_t k = 0; k < anchorTextSlices.size(); ++k)
    {
      if (k > 0)
      {
        anchorText += string(" ");
      }
      anchorText += anchorTextSlices[k].toString();
    }
    crawler::FullURLEntry outEntry;
    outEntry.entry.url = normalized;
    outEntry.anchorText = anchorText;
    outEntry.entry.depth = depth;

    state.outboundMu.lock();
    state.outbound[owner].pushBack(outEntry);
    state.outboundMu.unlock();
  }
}

void processBatchEntry(const urlEntry& entry, globalCrawlerState& state, URLNormalizer& normalizer,
                       fetcher& theFetcher)
{

  state.processedCount->fetch_add(1);

  string crawlUrl = https_url_helper(normalizer, entry.url);

  if (crawlUrl.size() == 0 || !hasValidHost(normalizer, crawlUrl))
  {
    state.crawlLogger->log(entry.url, "SKIP, INVALID URL");
    return;
  }

  if (state.filter->isBlocked(crawlUrl))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, BLOCKED URL");
    return;
  }

  if (hasBlockedExtension(normalizer, crawlUrl))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, BLOCKED EXTENSION");
    return;
  }

  const bool allowed =
      state.robotsAgent->allow(crawlUrl, normalizer, theFetcher, *state.crawlLogger);

  if (!allowed)
  {
    return;
  }

  auto result = theFetcher.fetch(crawlUrl);
  if (!result.hasValue())
  {
    string msg = "FAILED FETCHING: ";
    msg += result.error().msg;
    state.crawlLogger->log(crawlUrl, msg.cstr());
    return;
  }
  fetchResponse resp = result.value();

  if (resp.statusCode != 200)
  {
    state.crawlLogger->log(crawlUrl, "SKIP, BAD RESP", resp.statusCode);
    return;
  }

  if (!fetcher::isEnglishContentLanguage(resp.headers))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, NON-ENGLISH CONTENT-LANGUAGE");
    return;
  }

  if (!fetcher::isHtmlContentType(resp.headers))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, NON-HTML CONTENT");
    return;
  }

  if (!fetcher::isEnglishHtmlLang(resp.body))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, NON-ENGLISH HTML LANG");
    return;
  }
  state.crawlLogger->log(crawlUrl, "OK, Code 200", resp.body.size());

  HtmlParser parser(resp.body.cstr(), resp.body.size());
  if (parser.rejected)
  {
    state.crawlLogger->log(crawlUrl, "SKIP, TOO MUCH NON-ASCII");
    return;
  }

  if (responseContainsBlockedWords(*state.filter, parser))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, BLOCKED RESPONSE");
    return;
  }

  // TODO: add ranker to this
  DocumentMetadata documentMetadata = DocumentMetadata{
      .url = crawlUrl,
      .title = parser.rawTitle,
      .input = 0,
  };
  indexDocument(documentMetadata, parser.words);

  state.indexedCount->fetch_add(1);

  for (size_t j = 0; j < parser.links.size(); ++j)
  {
    string baseURL = crawlUrl;
    if (parser.base.size() > 0)
    {
      string parserBase = parser.base.toString();
      string resolvedBase = normalizer.normalize(crawlUrl, parserBase);
      resolvedBase = https_url_helper(normalizer, resolvedBase);
      if (resolvedBase.size() > 0)
      {
        baseURL = resolvedBase;
      }
    }

    string linkUrl = parser.links[j].URL.toString();
    string normalized = normalizer.normalize(baseURL, linkUrl);
    normalized = https_url_helper(normalizer, normalized);
    if (normalized.size() == 0)
    {
      continue;
    }

    if (!hasValidHost(normalizer, normalized))
    {
      state.crawlLogger->log(normalized, "SKIP, INVALID NORMALIZED URL");
      continue;
    }

    if (hasBlockedExtension(normalizer, normalized))
    {
      state.crawlLogger->log(normalized, "SKIP, BLOCKED EXTENSION");
      continue;
    }

    if (anchorContainsBlockedWords(*state.filter, parser.links[j]))
    {
      state.crawlLogger->log(normalized, "SKIP, BLOCKED ANCHOR TEXT");
      continue;
    }

    if (state.filter->isBlocked(normalized))
    {
      state.crawlLogger->log(normalized, "SKIP, BLOCKED URL");
      continue;
    }

    routeURLToPeer(normalized, entry.depth + 1, parser.links[j].anchorText, state);
  }
}

void* crawlBatchWorker(void* raw)
{
  threadArgs* args = static_cast<threadArgs*>(raw);
  globalCrawlerState& state = *args->state;
  URLNormalizer normalizer;
  fetcher theFetcher;

  while (true)
  {
    state.queueMu.lock();
    while (state.q.empty() && !state.shuttingDown)
    {
      state.queueCV.wait(state.queueMu);
    }

    if (state.shuttingDown && state.q.empty())
    {
      state.queueMu.unlock();
      break;
    }

    urlEntry entry = state.q.front();
    state.q.popFront();
    state.queueMu.unlock();

    if (!stopRequested())
    {
      processBatchEntry(entry, state, normalizer, theFetcher);
    }

    state.queueMu.lock();
    if (state.q.empty())
    {
      state.batchDoneCV.signal();
    }
    state.queueMu.unlock();
  }

  return nullptr;
}
} // namespace

int main()
{
  signal(SIGINT, handleStopSignal);
  signal(SIGTERM, handleStopSignal);
  signal(SIGPIPE, SIG_IGN);

  // Read cluster configuration from environment variables.
  size_t workerCount = 128;
  {
    const char* nodeIdEnv = getenv("NODE_ID");
    const char* numPeersEnv = getenv("NUM_PEERS");
    const char* workerCountEnv = getenv("CRAWLER_WORKERS");
    localNodeId = nodeIdEnv ? static_cast<size_t>(atoi(nodeIdEnv)) : 0;
    numPeers = numPeersEnv ? static_cast<size_t>(atoi(numPeersEnv)) : 1;
    if (numPeers < 1)
    {
      numPeers = 1;
    }
    if (workerCountEnv && atoi(workerCountEnv) > 0)
    {
      workerCount = static_cast<size_t>(atoi(workerCountEnv));
    }
  }

  if (numPeers > 1)
  {
    avengine::runNode(static_cast<uint64_t>(localNodeId), numPeers);
  }

  // Crawler Components
  frontier theFrontier;
  URLNormalizer normalizer;
  domainFilter filter("banned_urls.txt", "banned_words.txt");
  CrawlLogger crawlLogger("crawl_log.csv");

  // ── Seed URLs ──
  vector<string> seeds;
  fill_seeds(seeds, normalizer, "seed_list.txt");

  // Filter domains of the seed list
  vector<string> filteredSeeds;
  for (size_t i = 0; i < seeds.size(); ++i)
  {
    if (!filter.isBlocked(seeds[i]))
    {
      filteredSeeds.pushBack(seeds[i]);
    }
  }

  // Seed URLs to URLFrontier
  theFrontier.seed(filteredSeeds);

  // Crawling metadata
  atomic_counter processedCount(0);
  atomic_counter indexedCount(0);
  size_t checkpointInterval = CHECKPOINT_INTERVAL;
  const string checkpointPath = "frontier_checkpoint.bin";
  Robots_Agent robotsAgent(kRobotsUserAgent);

  globalCrawlerState workerState;
  workerState.theFrontier = &theFrontier;
  workerState.filter = &filter;
  workerState.crawlLogger = &crawlLogger;
  workerState.robotsAgent = &robotsAgent;
  workerState.processedCount = &processedCount;
  workerState.indexedCount = &indexedCount;
  // Pre-size per-peer outbound buffers (one slot per peer, including ourselves).
  workerState.outbound = vector<vector<crawler::FullURLEntry>>(numPeers);

  thread* workers = new thread[workerCount];
  threadArgs* workerArgs = new threadArgs[workerCount];
  size_t startedWorkers = 0;
  for (size_t i = 0; i < workerCount; ++i)
  {
    workerArgs[i].state = &workerState;
    if (workers[i].start(crawlBatchWorker, &workerArgs[i]))
    {
      ++startedWorkers;
    }
  }
  fetcher fallbackFetcher;

  resumeIndexer();

  // ── Main crawl loop ──
  while (!stopRequested())
  {
    // Drain any URLs received from peer nodes into the local frontier.
    if (numPeers > 1)
    {
      vector<crawler::FullURLEntry> received;
      crawler::drainInbound(received);
      if (!received.empty())
      {
        crawlLogger.log("PEER INBOUND", received.size(), "URLs received from peers");
        workerState.frontierMu.lock();
        for (size_t i = 0; i < received.size(); ++i)
        {
          theFrontier.add(received[i].entry.url, received[i].entry.depth);
        }
        workerState.frontierMu.unlock();
      }
    }

    workerState.frontierMu.lock();
    const size_t frontierSize = theFrontier.size();
    workerState.frontierMu.unlock();
    if (frontierSize == 0)
    {
      break;
    }

    workerState.frontierMu.lock();
    vector<urlEntry> batch = theFrontier.nextBatch(workerCount * 8, workerCount);
    workerState.frontierMu.unlock();

    if (batch.size() == 0)
    {
      break;
    }
    batch = filterBlockedBatch(batch, filter, crawlLogger);

    if (batch.size() == 0)
    {
      continue;
    }

    if (startedWorkers == 0) // For single threaded
    {
      for (size_t i = 0; i < batch.size() && !stopRequested(); ++i)
      {
        processBatchEntry(batch[i], workerState, normalizer, fallbackFetcher);
      }
    }
    else
    {
      workerState.queueMu.lock();
      for (size_t i = 0; i < batch.size() && !stopRequested(); ++i)
      {
        workerState.q.pushBack(batch[i]);
      }
      workerState.queueCV.broadcast();
      while (!workerState.q.empty())
      {
        workerState.batchDoneCV.wait(workerState.queueMu);
      }
      workerState.queueMu.unlock();
    }

    // Flush outbound URL buffers to the responsible peer nodes.
    if (numPeers > 1)
    {
      for (size_t peerId = 0; peerId < numPeers; ++peerId)
      {
        if (peerId == localNodeId)
        {
          continue;
        }

        vector<crawler::FullURLEntry> batch;
        workerState.outboundMu.lock();
        workerState.outbound[peerId].swap(batch);
        workerState.outbound[peerId].clear();
        workerState.outboundMu.unlock();

        if (batch.empty())
        {
          continue;
        }

        vector<uint8_t> encoded = crawler::encodeFrontierBatch(batch);
        if (avengine::sendMessage(peerId, avengine::MessageType::FRONTIER_UPDATE, encoded))
        {
          crawlLogger.log("PEER OUTBOUND FAILED", batch.size(), "URLs dropped to peer", peerId);
        }
        else
        {
          crawlLogger.log("PEER OUTBOUND", batch.size(), "URLs sent to peer", peerId);
        }
      }
    }

    const size_t processedSnapshot = static_cast<size_t>(processedCount.get_val());

    // Checkpoint all of our progress and log stats.
    if (processedSnapshot >= checkpointInterval)
    {
      checkpointInterval += CHECKPOINT_INTERVAL;
      workerState.frontierMu.lock();
      theFrontier.saveCheckpoint(checkpointPath);
      const size_t seenSnapshot = theFrontier.seenCount();
      const size_t frontierSnapshot = theFrontier.size();
      workerState.frontierMu.unlock();
      crawlLogger.log("CHECKPOINT", processedSnapshot, seenSnapshot, frontierSnapshot);
    }
  }

  workerState.queueMu.lock();
  workerState.shuttingDown = true;
  workerState.queueCV.broadcast();
  workerState.queueMu.unlock();
  for (size_t i = 0; i < workerCount; ++i)
  {
    if (workers[i].joinable())
    {
      workers[i].join();
    }
  }
  delete[] workers;
  delete[] workerArgs;

  const size_t processedSnapshot = static_cast<size_t>(processedCount.get_val());

  // Final checkpoint
  workerState.frontierMu.lock();
  theFrontier.saveCheckpoint(checkpointPath);
  const size_t seenSnapshot = theFrontier.seenCount();
  workerState.frontierMu.unlock();
  if (stopRequested())
  {
    crawlLogger.log("DONE, SIGNAL STOP", processedSnapshot, seenSnapshot);
  }
  else
  {
    crawlLogger.log("DONE, FINAL CHECKPOINT", processedSnapshot, seenSnapshot);
  }

  cleanUpIndexer();

  return 0;
}
