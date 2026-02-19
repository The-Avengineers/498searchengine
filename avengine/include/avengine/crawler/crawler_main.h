
// TODO THREAD POOL AROUND ALL THIS

int main()
{
  // Seed URLs to URLFrontier.                                               KINDA DONE (need seed
  // list) normalize seed URLs with URLNormalizer (normalizeIndividual)            KINDA DONE (needs
  // testing) DomainFilter.isBlocked()         filter domain                          KINDA DONE
  // (needs testing + blocked list) while (!done) { frontier.next()           get URL KINDA DONE
  // (needs testing) RobotsParser.isAllowed()     check robots.txt                        NOT DONE
  // HTTPFetcher.fetch(url)       GET the page                            KINDA DONE (needs testing)
  // if bad status/content-type   {continue}
  // check for 200 OK and content-type text/html, otherwise skip
  // HTMLParser.parse(response)   extract text + links                    KINDA DONE (wire in then
  // test) Indexer.index(url, content)  give indexer content                    NOT DONE for each
  // extracted link:
  //     normalize(base, link)      pass in the normalized base + link    KINDA DONE (NEEDS TESTING)
  //     DomainFilter.isBlocked()         filter domain                   KINDA DONE (NEEDS TESTING)
  //     if BloomFilter says unseen {frontier.add(link)}                  DONE
  // CrawlLogger.log(url, status, timestamp)                              NOT DONE
  // periodically checkpoint frontier + bloom state                       KINDA DONE (NEEDS TESTING)
  // } // while
  return;
}