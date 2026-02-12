
int main() {
   // Seed URLs to URLFrontier.                                 KINDA DONE
   // while (!done) {
      // frontier.next()           get URL                      KINDA DONE
      // URLNormalizer.normalize()    normalize URL             KINDA DONE
      // DomainFilter.check()         filter domain             NOT DONE   
      // RobotsParser.isAllowed()     check robots.txt          NOT DONE
      // HTTPFetcher.fetch(url)       GET the page              KINDA DONE
      // if bad status/content-type   {continue}                
      // HTMLParser.parse(response)   extract text + links      KINDA DONE
      // Indexer.index(url, content)  give indexer content      NOT DONE
      // for each extracted link:       
      //     normalize
      //     if BloomFilter says unseen {frontier.add(link)}
      // CrawlLogger.log(url, status, timestamp)                NOT DONE
      // periodically checkpoint frontier + bloom state         NOT DONE
  // } // while
    return;
}