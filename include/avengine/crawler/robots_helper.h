#pragma once

#include "../Utf8.h"
#include "../avengine_lib/string.h"
#include "../avengine_lib/vector.h"
#include "RobotsTxt.h"
#include "fetcher.h"
#include "logger.h"
#include "url_normalizer.h"
#include <time.h>

namespace avengine
{
namespace crawler
{

class Robots_Agent
{
public:
  explicit Robots_Agent(const Utf8* userAgent);
  ~Robots_Agent();

  // Requires: crawlUrl is a normalized absolute URL
  // Modifys: internal robots cache and delay timers
  // Returns: true when URL is allowed by robots policy
  bool allow(const string& crawlUrl, URLNormalizer& normalizer, fetcher& theFetcher,
             CrawlLogger& crawlLogger);

private:
  struct Robots_Rules
  {
    string origin;
    RobotsTxt* rules;
    int crawlDelaySeconds;
    time_t lastRequestTime;
    bool initialized;

    Robots_Rules();
  };

  vector<Robots_Rules> policies_;
  const Utf8* userAgent_;

  // Requires: parsed.host and parsed.protocol
  // Modifys: nothing
  // Effects: returns the origin key in the form protocol://host[:port]
  string buildOrigin(const URL& parsed) const;

  // Requires: origin to be == protocol://host[:port]
  // Modifys: policies_
  // Effects: returns a pointer to the cached policy entry for origin
  Robots_Rules* getOrCreatePolicy(const string& origin);

  // Requires: policy from getOrCreatePolicy
  // Modifys: policy
  // Effects: fetches and parses origin/robots.txt once then adds rules,
  //          may append log lines via crawlLogger, may perform network fetch via theFetcher
  void ensurePolicyLoaded(Robots_Rules& policy, fetcher& theFetcher, CrawlLogger& crawlLogger);

  // Requires: policy.crawlDelaySeconds and lastRequestTime
  // Modifys: thread sleeps TODO: ADD YIELD?
  // Effects: blocks until it is allowed to crawl
  void enforceCrawlDelay(const Robots_Rules& policy) const;

  // Requires: nothing
  // Modifys: polocies_
  // Effects: deleted rules and policy pointers nullptr
  void clearPolicies();
};

} // namespace crawler
} // namespace avengine
