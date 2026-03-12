#include "../../include/avengine/crawler/domain_filter.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/frontier.h"
#include "../../include/avengine/crawler/logger.h"
#include "../../include/avengine/crawler/url_normalizer.h"
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
  CrawlLogger crawlLogger("crawl_log.csv");

  // ── Seed URLs ──
  // normalize seed URLs with URLNormalizer (normalizeIndividual)
  vector<string> seeds;
  seeds.pushBack("https://en.wikipedia.org/wiki/Main_Page");
  seeds.pushBack("https://news.ycombinator.com/");
  seeds.pushBack("https://www.bbc.com/news");
  seeds.pushBack("https://www.nasa.gov/");
  seeds.pushBack("https://www.cdc.gov/");
  seeds.pushBack("https://www.gutenberg.org/");
  seeds.pushBack("https://openlibrary.org/");
  seeds.pushBack("https://www.loc.gov/");
  seeds.pushBack("https://arxiv.org/");
  seeds.pushBack("https://www.w3.org/");

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
    // Grab batch: sample 100, sort by depth, return top 10
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

      // GET request using the fetcher                                        KINDA DONE (needs
      // testing)
      auto result = theFetcher.fetch(entry.url);
      if (!result.hasValue())
      {
        crawlLogger.log(entry.url, "FAILED FETCHING");
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

      // Indexer.index(url, content)  give indexer content                    NOT DONE
      // indexer.index(entry.url, parser.words, parser.titleWords);

      // for each extracted link:
      for (size_t j = 0; j < parser.links.size(); ++j)
      {
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
          theFrontier.add(normalized, entry.depth + 1);
        }
      }

      crawlCount++;
    }

    // checkpoint
    if (crawlCount >= checkpointInterval)
    {
      checkpointInterval += 100;
      theFrontier.saveCheckpoint("frontier_checkpoint.bin");
      crawlLogger.log("CHECKPOINT", crawlCount, theFrontier.seenCount(), theFrontier.size());
    }
  }

  // Final checkpoint
  theFrontier.saveCheckpoint("frontier_checkpoint.bin");
  crawlLogger.log("DONE, FINAL CHECKPOINT", crawlCount, theFrontier.seenCount());

  return 0;
}
