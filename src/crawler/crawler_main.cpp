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
#include "avengine/avengine_lib/vector.h"
#include "avengine/crawler/crawler_structs.h"
#include <avengine/indexer/anchor_indexer.h>
#include <avengine/indexer/indexer.h>
#include <avengine/node.h>
#include <csignal>
#include <cstdlib>
#include <ctime>
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
bool hasBlockedExtension(const string& path)
{
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

// GitHub-specific crawl guard: only allow
//   /
//   /<user_or_org>
//   /<owner>/<repo>
// and block deeper paths like /<owner>/<repo>/issues, /blob, /tree, etc.
bool hasAllowedGitHubPathDepth(const string& path)
{
  size_t segmentCount = 0;
  size_t i = 0;

  while (i < path.size())
  {
    while (i < path.size() && path[i] == '/')
    {
      ++i;
    }

    if (i >= path.size() || path[i] == '?')
    {
      break;
    }

    ++segmentCount;
    if (segmentCount > 2)
    {
      return false;
    }

    while (i < path.size() && path[i] != '/' && path[i] != '?')
    {
      ++i;
    }

    if (i < path.size() && path[i] == '?')
    {
      break;
    }
  }

  return true;
}

bool passesGitHubCrawlPolicy(URLNormalizer& normalizer, const string& url)
{
  URL parsed = normalizer.parseURL(url);
  if (parsed.host != "github.com")
  {
    return true;
  }

  return hasAllowedGitHubPathDepth(parsed.path);
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

// Same behavior as frontier::nextBatch, but minimizes time spent holding the
// frontier list mutex by doing sorting outside the critical section.
vector<urlEntry> nextBatchShortFrontierLock(frontier& theFrontier, mutex& frontierListMu,
                                            size_t sampleSize, size_t batchSize)
{
  vector<urlEntry> batch;

  frontierListMu.lock();
  vector<urlEntry> sample = theFrontier.takeRandomSample(sampleSize);
  frontierListMu.unlock();

  if (sample.empty())
  {
    return batch;
  }

  // Sort sample by depth in ascending order (closer to seed first).
  sample.sort([](const urlEntry& lhs, const urlEntry& rhs) { return lhs.depth < rhs.depth; });

  const size_t actualBatchSize = (batchSize < sample.size()) ? batchSize : sample.size();
  batch.reserve(actualBatchSize);
  for (size_t i = 0; i < actualBatchSize; ++i)
  {
    batch.pushBack(sample[i]);
  }

  frontierListMu.lock();
  theFrontier.appendBatch(sample, actualBatchSize);
  frontierListMu.unlock();

  return batch;
}

struct globalCrawlerState
{
  frontier* theFrontier;
  domainFilter* filter;
  CrawlLogger* crawlLogger;
  Robots_Agent* robotsAgent;
  // Split frontier synchronization to reduce contention:
  // - seenMu guards Bloom filter deduplication
  // - frontierListMu guards frontier masterList mutations/sampling
  mutex seenMu;
  mutex frontierListMu;
  mutex queueMu;
  condition_variable queueCV;
  condition_variable batchDoneCV;
  deque<urlEntry> q;

  // Workers enqueue discovered local URLs here. Producer is the sole merger
  // into frontier seen/masterList (single-writer for frontier state).
  mutex discoveredMu;
  deque<urlEntry> discoveredLocal;

  bool shuttingDown;
  atomic_counter* processedCount;
  atomic_counter* indexedCount;
  atomic_counter* failedCount;
  // Per-peer outbound URL buffers for distributed crawling.
  // Sized to numPeers during setup; entry [localNodeId] is never used.
  vector<vector<crawler::FullURLEntry>> outbound;
  mutex outboundMu;

  globalCrawlerState()
      : theFrontier(nullptr), filter(nullptr), crawlLogger(nullptr), robotsAgent(nullptr), q(),
        shuttingDown(false), processedCount(nullptr), indexedCount(nullptr), failedCount(nullptr)
  {
  }
};

struct threadArgs
{
  globalCrawlerState* state;
};

struct producerArgs
{
  globalCrawlerState* state;
  size_t workerCount;
};

void mergeDiscoveredIntoFrontier(globalCrawlerState& state)
{
  vector<urlEntry> discovered;
  state.discoveredMu.lock();
  while (!state.discoveredLocal.empty())
  {
    discovered.pushBack(state.discoveredLocal.front());
    state.discoveredLocal.popFront();
  }
  state.discoveredMu.unlock();

  if (discovered.empty())
  {
    return;
  }

  vector<urlEntry> toAppend;
  state.seenMu.lock();
  for (size_t i = 0; i < discovered.size(); ++i)
  {
    const string& url = discovered[i].url;
    if (state.theFrontier->alreadySeen(url))
    {
      continue;
    }

    state.theFrontier->markSeen(url);
    toAppend.pushBack(discovered[i]);
  }
  state.seenMu.unlock();

  if (!toAppend.empty())
  {
    state.frontierListMu.lock();
    state.theFrontier->appendBatch(toAppend);
    state.frontierListMu.unlock();
  }
}

// Routes a normalized URL (in a multi-peer crawl) to the correct node. If this
// node owns the URL it is added directly to the local frontier; otherwise it
// is enqueued in the appropriate outbound buffer so that the network-dispatch
// thread can forward it to the owning peer.
void routeURLToPeer(const string& normalized, int depth,
                    const vector<string_slice>& anchorTextSlices, globalCrawlerState& state,
                    vector<urlEntry>& localLinks)
{
  size_t owner = hashString(normalized) % numPeers;
  if (owner == localNodeId)
  {
    urlEntry entry;
    entry.url = normalized;
    entry.domain = state.theFrontier->extractDomain(normalized);
    entry.depth = depth;

    localLinks.pushBack(entry);

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
                       fetcher& theFetcher, avengine::vector<urlEntry>& preparedURLs)
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

  if (hasBlockedExtension(crawlUrl))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, BLOCKED EXTENSION");
    return;
  }

  if (!passesGitHubCrawlPolicy(normalizer, crawlUrl))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, GITHUB DEPTH POLICY");
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
    state.failedCount->fetch_add(1);
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
      .description = parser.rawDescription,
      .input = ranking_input::Pack(entry.depth < 0 ? 0u : static_cast<uint32_t>(entry.depth),
                                   static_cast<uint32_t>(crawlUrl.size()),
                                   ranking_input::CountWords(parser.rawTitle)),
  };
  indexDocument(documentMetadata, parser.words);

  state.indexedCount->fetch_add(1);

  size_t routedLinks = 0;
  const size_t kMaxRoutedLinksPerPage = 400;

  // Resolve page-level <base> once instead of per-link.
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

  for (size_t j = 0; j < parser.links.size(); ++j)
  {
    if (routedLinks >= kMaxRoutedLinksPerPage)
    {
      break;
    }

    const string_slice linkUrlSlice = parser.links[j].URL;
    if (linkUrlSlice.size() == 0)
    {
      continue;
    }

    // Cheap scheme/fragment pre-filter before expensive normalization.
    if (linkUrlSlice[0] == '#')
    {
      continue;
    }

    if (linkUrlSlice.startsWith("javascript:") || linkUrlSlice.startsWith("mailto:") ||
        linkUrlSlice.startsWith("tel:") || linkUrlSlice.startsWith("data:"))
    {
      continue;
    }

    string linkUrl = linkUrlSlice.toString();
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

    if (hasBlockedExtension(normalized))
    {
      state.crawlLogger->log(normalized, "SKIP, BLOCKED EXTENSION");
      continue;
    }

    if (!passesGitHubCrawlPolicy(normalizer, normalized))
    {
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

    routeURLToPeer(normalized, entry.depth + 1, parser.links[j].anchorText, state, preparedURLs);
    ++routedLinks;
  }
}

void flushPreparedURLsToDiscovered(globalCrawlerState& state, vector<urlEntry>& preparedURLs)
{
  if (preparedURLs.empty())
  {
    return;
  }

  state.discoveredMu.lock();
  for (size_t i = 0; i < preparedURLs.size(); ++i)
  {
    state.discoveredLocal.pushBack(preparedURLs[i]);
  }
  state.discoveredMu.unlock();

  preparedURLs.clear();
}

void* crawlBatchProducer(void* raw)
{
  producerArgs* args = static_cast<producerArgs*>(raw);
  globalCrawlerState& state = *args->state;
  const size_t workerCount = args->workerCount;

  while (!stopRequested())
  {
    state.queueMu.lock();
    while (!state.q.empty() && !state.shuttingDown && !stopRequested())
    {
      state.batchDoneCV.wait(state.queueMu);
    }

    if (state.shuttingDown || stopRequested())
    {
      state.queueMu.unlock();
      break;
    }
    state.queueMu.unlock();

    mergeDiscoveredIntoFrontier(state);

    size_t sampleSize = workerCount * 8 * 16;
    if (sampleSize > 8192)
    {
      sampleSize = 8192;
    }
    size_t batchSize = workerCount * 16;
    if (batchSize > sampleSize)
    {
      batchSize = sampleSize;
    }

    vector<urlEntry> batch =
        nextBatchShortFrontierLock(*state.theFrontier, state.frontierListMu, sampleSize, batchSize);
    batch = filterBlockedBatch(batch, *state.filter, *state.crawlLogger);

    if (batch.empty())
    {
      usleep(1000);
      continue;
    }

    state.queueMu.lock();
    if (state.shuttingDown)
    {
      state.queueMu.unlock();
      break;
    }

    if (state.q.empty())
    {
      for (size_t i = 0; i < batch.size() && !stopRequested(); ++i)
      {
        state.q.pushBack(batch[i]);
      }
      if (!state.q.empty())
      {
        state.queueCV.broadcast();
      }
    }
    state.queueMu.unlock();
  }

  // Flush any discovered URLs left by workers before producer exits.
  mergeDiscoveredIntoFrontier(state);

  return nullptr;
}

void* crawlBatchWorker(void* raw)
{
  threadArgs* args = static_cast<threadArgs*>(raw);
  globalCrawlerState& state = *args->state;
  URLNormalizer normalizer;
  fetcher theFetcher;
  avengine::vector<urlEntry> preparedURLs;
  size_t runCount = 0;

  while (true)
  {
    runCount++;
    state.queueMu.lock();

    while (state.q.empty() && !state.shuttingDown)
    {
      state.batchDoneCV.signal();
      state.queueCV.wait(state.queueMu);
    }

    if (state.shuttingDown && state.q.empty())
    {
      state.queueMu.unlock();
      flushPreparedURLsToDiscovered(state, preparedURLs);
      break;
    }

    if (state.q.empty())
    {
      state.queueMu.unlock();
      continue;
    }

    urlEntry entry = state.q.front();
    state.q.popFront();
    if (state.q.empty())
    {
      state.batchDoneCV.signal();
    }
    state.queueMu.unlock();

    if (!stopRequested())
    {
      processBatchEntry(entry, state, normalizer, theFetcher, preparedURLs);
    }

    // Force frequent flushes during warmup so workers quickly feed the
    // producer/frontier and we avoid early frontier stalls.
    if (preparedURLs.size() > 1500 || runCount < 10)
    {
      state.crawlLogger->log("UPDATING DISCOVERED", preparedURLs.size());

      flushPreparedURLsToDiscovered(state, preparedURLs);
    }
  }

  return nullptr;
}
} // namespace

int main()
{
  signal(SIGINT, handleStopSignal);
  signal(SIGTERM, handleStopSignal);
  signal(SIGPIPE, SIG_IGN);

  fetcher::initSharedContext();

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
  CrawlLogger& crawlLogger = globalCrawlLogger();

  const string checkpointPath = "frontier_checkpoint.bin";

  bool resumedFromCheckpoint = false;
  if (access(checkpointPath.cstr(), F_OK) == 0)
  {
    auto loadRes = theFrontier.loadCheckpoint(checkpointPath);
    if (loadRes.hasValue())
    {
      resumedFromCheckpoint = true;
      crawlLogger.log("RESUME", "LOADED CHECKPOINT", checkpointPath, theFrontier.size(),
                      theFrontier.seenCount());
    }
    else
    {
      crawlLogger.log("RESUME", "CHECKPOINT LOAD FAILED", checkpointPath, loadRes.error().msg);
    }
  }

  if (!resumedFromCheckpoint)
  {
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
    crawlLogger.log("RESUME", "SEEDED FRONTIER", filteredSeeds.size());
  }

  // Crawling metadata
  atomic_counter processedCount(0);
  atomic_counter indexedCount(0);
  size_t checkpointInterval = CHECKPOINT_INTERVAL;
  atomic_counter failedCount(0);
  Robots_Agent robotsAgent(kRobotsUserAgent);

  globalCrawlerState workerState;
  workerState.theFrontier = &theFrontier;
  workerState.filter = &filter;
  workerState.crawlLogger = &crawlLogger;
  workerState.robotsAgent = &robotsAgent;
  workerState.processedCount = &processedCount;
  workerState.indexedCount = &indexedCount;
  workerState.failedCount = &failedCount;
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

  thread producerThread;
  producerArgs producerThreadArgs = {.state = &workerState, .workerCount = workerCount};
  bool producerStarted = false;
  if (startedWorkers > 0)
  {
    producerStarted = producerThread.start(crawlBatchProducer, &producerThreadArgs);
  }

  resumeIndexer();

  // Tracking variables for per-second rate logging around nextBatch.
  struct timespec crawlStartTime;
  clock_gettime(CLOCK_MONOTONIC, &crawlStartTime);
  struct timespec batchRateLastTime = crawlStartTime;
  size_t batchRateLastIndexed = 0;
  size_t batchRateLastFailed = 0;

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

        vector<urlEntry> toAppend;
        workerState.seenMu.lock();
        for (size_t i = 0; i < received.size(); ++i)
        {
          const string& inboundUrl = received[i].entry.url;
          if (theFrontier.alreadySeen(inboundUrl))
          {
            continue;
          }

          theFrontier.markSeen(inboundUrl);

          urlEntry entry;
          entry.url = inboundUrl;
          entry.domain = theFrontier.extractDomain(inboundUrl);
          entry.depth = received[i].entry.depth;
          toAppend.pushBack(entry);
        }
        workerState.seenMu.unlock();

        if (!toAppend.empty())
        {
          workerState.frontierListMu.lock();
          theFrontier.appendBatch(toAppend);
          workerState.frontierListMu.unlock();
        }
      }
    }

    usleep(1000);

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

    {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      double elapsed = (now.tv_sec - batchRateLastTime.tv_sec) +
                       (now.tv_nsec - batchRateLastTime.tv_nsec) * 1e-9;
      if (elapsed >= 5.0)
      {
        size_t curIndexed = static_cast<size_t>(indexedCount.get_val());
        size_t curFailed = static_cast<size_t>(failedCount.get_val());
        double successRate = (curIndexed - batchRateLastIndexed) / elapsed;
        double failRate = (curFailed - batchRateLastFailed) / elapsed;
        crawlLogger.log("STATUS", successRate, failRate);
        batchRateLastIndexed = curIndexed;
        batchRateLastFailed = curFailed;
        batchRateLastTime = now;
      }
    }

    const size_t processedSnapshot = static_cast<size_t>(processedCount.get_val());

    // Checkpoint all of our progress and log stats.
    if (processedSnapshot >= checkpointInterval)
    {
      checkpointInterval += CHECKPOINT_INTERVAL;
      workerState.seenMu.lock();
      workerState.frontierListMu.lock();
      theFrontier.saveCheckpoint(checkpointPath);
      const size_t seenSnapshot = theFrontier.seenCount();
      const size_t frontierSnapshot = theFrontier.size();
      workerState.frontierListMu.unlock();
      workerState.seenMu.unlock();
      crawlLogger.log("CHECKPOINT", processedSnapshot, seenSnapshot, frontierSnapshot);
    }
  }

  workerState.queueMu.lock();
  workerState.shuttingDown = true;
  workerState.queueCV.broadcast();
  workerState.batchDoneCV.broadcast();
  workerState.queueMu.unlock();

  if (producerStarted && producerThread.joinable())
  {
    producerThread.join();
  }

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
  workerState.seenMu.lock();
  workerState.frontierListMu.lock();
  theFrontier.saveCheckpoint(checkpointPath);
  const size_t seenSnapshot = theFrontier.seenCount();
  workerState.frontierListMu.unlock();
  workerState.seenMu.unlock();
  if (stopRequested())
  {
    crawlLogger.log("DONE, SIGNAL STOP", processedSnapshot, seenSnapshot);
  }
  else
  {
    crawlLogger.log("DONE, FINAL CHECKPOINT", processedSnapshot, seenSnapshot);
  }

  cleanUpIndexer();

  fetcher::cleanupSharedContext();

  return 0;
}
