#include "../../include/avengine/crawler/frontier.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/url_normalizer.h"
#include "../../include/avengine/crawler/domain_filter.h"
#include "../../include/avengine/html_parser/HtmlParser.h"

using namespace avengine;
using namespace avengine::crawler;

// TODO THREAD POOL AROUND ALL THIS

int main()
{
  // Crawler Components 
  frontier theFrontier;
  fetcher theFetcher;
  URLNormalizer normalizer;
  domainFilter filter;

  // ── Seed URLs ──
  // normalize seed URLs with URLNormalizer (normalizeIndividual)   KINDA DONE (needs testing)
  vector<string> seeds;
  // add seeds here

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

  size_t crawlCount = 0;
  size_t checkpointInterval = 100;

  // ── Main crawl loop ──
  while (theFrontier.size() > 0)
  {
    // Grab batch: sample 100, sort by priority, return top 10
    vector<urlEntry> batch = theFrontier.nextBatch(100, 10);

    if (batch.size() == 0)
    {
      break;
    }

    for (size_t i = 0; i < batch.size(); ++i)
    {
      const urlEntry& entry = batch[i];

      // RobotsParser.isAllowed()                                             NOT DONE
      // if (!robotsParser.isAllowed(entry.url)) continue;

      // GET request using the fetcher                                        KINDA DONE (needs testing)
      auto result = theFetcher.fetch(entry.url);
      if (!result.hasValue())
      {
        // CrawlLogger.log(entry.url, "FAIL")                                  NOT DONE
        continue;
      }

      fetchResponse resp = result.value();

      // check to ensure status == 200
      if (resp.statusCode != 200)
      {
        // CrawlLogger.log(entry.url, "SKIP", resp.statusCode)                  NOT DONE
        continue;
      }

      // CrawlLogger.log(entry.url, "OK", resp.body.size())                    NOT DONE

      // Call the constructor to parse the HTML                             KINDA DONE (TEST)
      HtmlParser parser(resp.body.cstr(), resp.body.size());

      // Indexer.index(url, content)  give indexer content                    NOT DONE
      // indexer.index(entry.url, parser.words, parser.titleWords);

      // for each extracted link:
      for (size_t j = 0; j < parser.links.size(); ++j)
      {
        // normalize(base, link)      pass in the normalized base + link    KINDA DONE (NEEDS TESTING)
        string baseURL;
        if (parser.base.size() > 0) 
        {
          baseURL = parser.base;
        }
        else 
        {
          baseURL = entry.url;
        }
        
        // normalize the domain
        string normalized = normalizer.normalize(baseURL, parser.links[j].URL);

        // filter the domain              
        if (filter.isBlocked(normalized))
        {
          continue;
        }

        // if BloomFilter says unseen then add to the frontier
        if (!theFrontier.alreadySeen(normalized))
        {
          theFrontier.add(normalized, /* NEED TO FIGURE OUT PRIORITY*/50, entry.depth + 1);
        }
      }

      // CrawlLogger.log(url, status, timestamp)
      // crawlLogger.log(entry.url, resp.statusCode, time(nullptr));

      crawlCount++;
    }

    // checkpoint
    if (crawlCount % checkpointInterval == 0 && crawlCount > 0)
    {
      theFrontier.saveCheckpoint("frontier_checkpoint.bin");
      // CrawlLogger.log("CHECKPOINT", crawlCount)                              NOT DONE
    }
  }

  // Final checkpoint
  theFrontier.saveCheckpoint("frontier_checkpoint.bin");
  // CrawlLogger.log("DONE", crawlCount, theFrontier.seenCount())              NOT DONE

  return 0;
}
