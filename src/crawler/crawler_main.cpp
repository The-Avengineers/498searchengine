#include "../../include/avengine/crawler/domain_filter.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/frontier.h"
#include "../../include/avengine/crawler/logger.h"
#include "../../include/avengine/crawler/url_normalizer.h"
#include "../../include/avengine/html_parser/HtmlParser.h"
#include "../index.h"
#include <fstream>
#include <signal.h>

using namespace avengine;
using namespace avengine::crawler;

// TODO THREAD POOL AROUND ALL THIS

namespace
{
volatile sig_atomic_t gStopRequested = 0;

void handleStopSignal(int)
{
  gStopRequested = 1;
}

bool stopRequested()
{
  return gStopRequested != 0;
}

bool hasValidHost(URLNormalizer& normalizer, const string& url)
{
  URL parsed = normalizer.parseURL(url);
  return parsed.host.size() > 0;
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

    string normalized = normalizer.normalizeIndividual(line);
    if (normalized.size() == 0)
    {
      continue;
    }
    seeds.pushBack(normalized);
  }
}

size_t estimateIndexMemoryBytes(const Index& index)
{
  size_t bytes = sizeof(Index);
  bytes += index.buckets.capacity() * sizeof(PostingList*);
  bytes += index.documentMetadata.capacity() * sizeof(DocumentMetadata);

  for (size_t i = 0; i < index.documentMetadata.size(); ++i)
  {
    const DocumentMetadata& doc = index.documentMetadata[i];
    bytes += doc.url.size();
    bytes += doc.title.size();
  }

  for (size_t i = 0; i < index.buckets.size(); ++i)
  {
    PostingList* bucket = index.buckets[i];
    while (bucket)
    {
      bytes += sizeof(PostingList);
      bytes += bucket->word.size();
      bytes += bucket->postings.capacity() * sizeof(Posting);
      bytes += bucket->positions.capacity() * sizeof(uint32_t);
      bucket = bucket->nextBucket;
    }
  }

  return bytes;
}

[[gnu::always_inline]] inline bool shouldLogIndexProgress(size_t indexedCount, size_t& nextCount)
{
  if (indexedCount < nextCount)
  {
    return false;
  }
  nextCount += 1000;
  return true;
}

} // namespace

int main()
{
  signal(SIGINT, handleStopSignal);
  signal(SIGTERM, handleStopSignal);

  // Crawler Components
  frontier theFrontier;
  fetcher theFetcher;
  URLNormalizer normalizer;
  domainFilter filter;
  CrawlLogger crawlLogger("crawl_log.csv");

  // ── Seed URLs ──
  vector<string> seeds;
  fill_seeds(seeds, normalizer, "seed_list.txt");

  // Filter domains of the seed list                      KINDA DONE (needs testing + blocked list)
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

  size_t processedCount = 0;
  size_t indexedCount = 0;
  size_t nextCount = 1000;
  size_t checkpointInterval = 100;
  const string checkpointPath = "frontier_checkpoint.bin";

  // ── Main crawl loop ──
  while (theFrontier.size() > 0 && !stopRequested())
  {
    // Grab batch: sample 100, sort by depth, return top 10
    vector<urlEntry> batch = theFrontier.nextBatch(100, 10);

    if (batch.size() == 0)
    {
      break;
    }

    for (size_t i = 0; i < batch.size() && !stopRequested(); ++i)
    {
      const urlEntry& entry = batch[i];
      processedCount++;

      // RobotsParser.isAllowed()                                             NOT DONE
      // if (!robotsParser.isAllowed(entry.url)) continue;

      // GET request using the fetcher                                        KINDA DONE (needs
      // testing)
      auto result = theFetcher.fetch(entry.url);
      if (!result.hasValue())
      {
        string msg = "FAILED FETCHING: ";
        msg += result.error().msg;
        crawlLogger.log(entry.url, msg.cstr());
        continue;
      }
      // ensure html text
      fetchResponse resp = result.value();

      // check to ensure status == 200
      if (resp.statusCode != 200)
      {
        crawlLogger.log(entry.url, "SKIP, BAD RESP", resp.statusCode);
        continue;
      }

      if (!fetcher::isHtmlContentType(resp.headers))
      {
        crawlLogger.log(entry.url, "SKIP, NON-HTML CONTENT");
        continue;
      }

      crawlLogger.log(entry.url, "OK, Code 200", resp.body.size());

      // Call the constructor to parse the HTML                             KINDA DONE (TEST)
      HtmlParser parser(resp.body.cstr(), resp.body.size());
      if (parser.rejected)
      {
        crawlLogger.log(entry.url, "SKIP, TOO MUCH NON-ASCII");
        continue;
      }


      // indexer.index(entry.url, parser.words, parser.titleWords);
      ++indexedCount;

      // for each extracted link:
      for (size_t j = 0; j < parser.links.size(); ++j)
      {
        string baseURL = entry.url;
        if (parser.base.size() > 0)
        {
          // <base href> can be relative or malformed; resolve against fetched URL.
          string parserBase = parser.base.toString();
          string resolvedBase = normalizer.normalize(entry.url, parserBase);
          if (resolvedBase.size() > 0)
          {
            baseURL = resolvedBase;
          }
        }

        // normalize the domain
        string linkUrl = parser.links[j].URL.toString();
        string normalized = normalizer.normalize(baseURL, linkUrl);
        if (normalized.size() == 0)
        {
          continue;
        }

        // Guard against malformed normalized URLs (e.g. empty host).
        if (!hasValidHost(normalizer, normalized))
        {
          crawlLogger.log(normalized, "SKIP, INVALID NORMALIZED URL");
          continue;
        }

        // filter the domain
        if (filter.isBlocked(normalized))
        {
          continue;
        }

        // if BloomFilter says unseen then add to the frontier
        if (!theFrontier.alreadySeen(normalized))
        {
          theFrontier.add(normalized, entry.depth + 1);
        }
      }

      if (shouldLogIndexProgress(indexedCount, nextCount))
      {
        crawlLogger.logIndexStats(indexedCount, estimateIndexMemoryBytes(globalIndex),
                                          theFrontier.size());
      }
    }

    // checkpoint
    if (processedCount >= checkpointInterval)
    {
      checkpointInterval += 100;
      theFrontier.saveCheckpoint(checkpointPath);
      crawlLogger.log("CHECKPOINT", processedCount, theFrontier.seenCount(), theFrontier.size());
    }
  }

  // Final checkpoint
  theFrontier.saveCheckpoint(checkpointPath);
  if (stopRequested())
  {
    crawlLogger.log("DONE, SIGNAL STOP", processedCount, theFrontier.seenCount());
  }
  else
  {
    crawlLogger.log("DONE, FINAL CHECKPOINT", processedCount, theFrontier.seenCount());
  }

  return 0;
}
