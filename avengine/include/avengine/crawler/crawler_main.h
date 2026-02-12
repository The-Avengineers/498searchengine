
int main() {
   // Seed URLs to URLFrontier.                                 KINDA DONE
   // normalize seed URLs with URLNormalizer (normalizeIndividual)            KINDA DONE
   // while (!done) {
      // frontier.next()           get URL                      KINDA DONE
      // DomainFilter.isBlocked()         filter domain             NOT DONE   
      // RobotsParser.isAllowed()     check robots.txt          NOT DONE
      // HTTPFetcher.fetch(url)       GET the page              KINDA DONE
      // if bad status/content-type   {continue}  
        //check for 200 OK and content-type text/html, otherwise skip            
      // HTMLParser.parse(response)   extract text + links      KINDA DONE
      // Indexer.index(url, content)  give indexer content      NOT DONE
      // for each extracted link:       
      //     normalize(base, link)      pass in the normalized base + link
      //     if BloomFilter says unseen {frontier.add(link)}
      // CrawlLogger.log(url, status, timestamp)                NOT DONE
      // periodically checkpoint frontier + bloom state         NOT DONE
  // } // while
    return;
}