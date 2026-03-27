#include "../../include/avengine/crawler/domain_filter.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/frontier.h"
#include "../../include/avengine/crawler/logger.h"
#include "../../include/avengine/crawler/robots_helper.h"
#include "../../include/avengine/crawler/url_normalizer.h"
#include "../../include/avengine/html_parser/HtmlParser.h"
#include "../index.h"
#include "avengine/avengine_lib/condition_variable.h"
#include "avengine/avengine_lib/deque.h"
#include "avengine/avengine_lib/mutex.h"
#include "avengine/avengine_lib/thread.h"
#include "avengine/avengine_lib/string.h"
#include "avengine/avengine_lib/string_slice.h"
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <unistd.h>

using namespace avengine;
using namespace avengine::crawler;

// TODO THREAD POOL AROUND ALL THIS

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
    if (line[0] == '\0')
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

struct globalCrawlerState
{
  frontier* theFrontier;
  domainFilter* filter;
  CrawlLogger* crawlLogger;
  Robots_Agent* robotsAgent;
  mutex frontierMu;
  mutex robotsMu;
  mutex queueMu;
  mutex countMu;
  condition_variable queueCV;
  condition_variable batchDoneCV;
  deque<urlEntry> q;
  size_t inFlight;
  bool shuttingDown;
  size_t* processedCount;
  size_t* indexedCount;

  globalCrawlerState()
      : theFrontier(nullptr), filter(nullptr), crawlLogger(nullptr), 
      robotsAgent(nullptr), q(), inFlight(0), shuttingDown(false), 
      processedCount(nullptr), indexedCount(nullptr)
  {}
};

struct threadArgs
{
  globalCrawlerState* state;
};

void processBatchEntry(const urlEntry& entry, globalCrawlerState& state, URLNormalizer& normalizer,
                       fetcher& theFetcher)
{

  state.countMu.lock();
  ++(*state.processedCount);
  state.countMu.unlock();

  string crawlUrl = https_url_helper(normalizer, entry.url);

  if (crawlUrl.size() == 0 || !hasValidHost(normalizer, crawlUrl))
  {
    state.crawlLogger->log(entry.url, "SKIP, INVALID URL");
    return;
  }

  state.robotsMu.lock();
  const bool allowed = state.robotsAgent->allow(crawlUrl, normalizer, theFetcher, *state.crawlLogger);
  state.robotsMu.unlock();

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

  if (!fetcher::isHtmlContentType(resp.headers))
  {
    state.crawlLogger->log(crawlUrl, "SKIP, NON-HTML CONTENT");
    return;
  }
  state.crawlLogger->log(crawlUrl, "OK, Code 200", resp.body.size());

  HtmlParser parser(resp.body.cstr(), resp.body.size());
  if (parser.rejected)
  {
    state.crawlLogger->log(crawlUrl, "SKIP, TOO MUCH NON-ASCII");
    return;
  }

  // TODO: add ranker to this
  DocumentMetadata documentMetadata = DocumentMetadata{
      .url = crawlUrl,
      .title = parser.fullTitle,
      .input = 0,
  };
  indexDocument(documentMetadata, parser.words);

  state.countMu.lock();
  ++(*state.indexedCount);
  state.countMu.unlock();

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

    if (state.filter->isBlocked(normalized))
    {
      continue;
    }

    state.frontierMu.lock();
    state.theFrontier->add(normalized, entry.depth + 1);
    state.frontierMu.unlock();
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
    ++state.inFlight;
    state.queueMu.unlock();

    if (!stopRequested())
    {
      processBatchEntry(entry, state, normalizer, theFetcher);
    }

    state.queueMu.lock();
    --state.inFlight;
    if (state.q.empty() && state.inFlight == 0)
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
  
  // Crawler Components
  frontier theFrontier;
  URLNormalizer normalizer;
  domainFilter filter;
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
  size_t processedCount = 0;
  size_t indexedCount = 0;
  size_t checkpointInterval = 100;
  const string checkpointPath = "frontier_checkpoint.bin";
  Robots_Agent robotsAgent(kRobotsUserAgent);

  globalCrawlerState workerState;
  workerState.theFrontier = &theFrontier;
  workerState.filter = &filter;
  workerState.crawlLogger = &crawlLogger;
  workerState.robotsAgent = &robotsAgent;
  workerState.processedCount = &processedCount;
  workerState.indexedCount = &indexedCount;

  size_t workerCount = 8; // MAKE BIGGER
  thread* workers = new thread[workerCount];
  threadArgs* threadArgs = new threadArgs[workerCount];
  size_t startedWorkers = 0;
  for (size_t i = 0; i < workerCount; ++i)
  {
    threadArgs[i].state = &workerState;
    if (workers[i].start(crawlBatchWorker, &threadArgs[i]))
    {
      ++startedWorkers;
    }
  }
  fetcher fallbackFetcher;

  // ── Main crawl loop ──
  while (!stopRequested())
  {
    workerState.frontierMu.lock();
    const size_t frontierSize = theFrontier.size();
    workerState.frontierMu.unlock();
    if (frontierSize == 0)
    {
      break;
    }

    workerState.frontierMu.lock();
    vector<urlEntry> batch = theFrontier.nextBatch(100, 10); // MAKE BIGGER
    workerState.frontierMu.unlock();

    if (batch.size() == 0)
    {
      break;
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
      while (!workerState.q.empty() || workerState.inFlight > 0)
      {
        workerState.batchDoneCV.wait(workerState.queueMu);
      }
      workerState.queueMu.unlock();
    }

    workerState.countMu.lock();
    const size_t processedSnapshot = processedCount;
    workerState.countMu.unlock();

    // Checkpoint all of our progress and log stats.
    if (processedSnapshot >= checkpointInterval)
    {
      checkpointInterval += 100;
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
  delete[] threadArgs;

  workerState.countMu.lock();
  const size_t processedSnapshot = processedCount;
  workerState.countMu.unlock();

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
