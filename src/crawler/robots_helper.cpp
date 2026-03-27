#include "../../include/avengine/crawler/robots_helper.h"
#include "../../include/avengine/crawler/RobotsTxt.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/logger.h"

#include <time.h>
#include <sys/time.h>
#include <unistd.h>

namespace avengine
{
namespace crawler
{

namespace
{

double nowSeconds()
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<double>(tv.tv_sec) + (static_cast<double>(tv.tv_usec) / 1000000.0);
}
}

Robots_Agent::Robots_Rules::Robots_Rules()
    : rules(nullptr), crawlDelaySeconds(0), lastRequestTime(0), initialized(false)
{
}

Robots_Agent::Robots_Agent(const Utf8* userAgent) : userAgent_(userAgent) {}

Robots_Agent::~Robots_Agent()
{
  clearPolicies();
}

string Robots_Agent::buildOrigin(const URL& parsed) const
{
  string origin = "https";
  origin += "://";
  origin += parsed.host;
  if (parsed.port.size() > 0)
  {
    origin += ":";
    origin += parsed.port;
  }
  return origin;
}

Robots_Agent::Robots_Rules* Robots_Agent::getOrCreatePolicy(const string& origin)
{
  for (size_t i = 0; i < policies_.size(); ++i)
  {
    if (policies_[i].origin == origin)
    {
      return &policies_[i];
    }
  }

  Robots_Rules created;
  created.origin = origin;
  policies_.pushBack(created);
  return &policies_[policies_.size() - 1];
}

void Robots_Agent::enforceCrawlDelay(double waitSeconds) const
{
  if (waitSeconds <= 0.0)
  {
    return;
  }
  const useconds_t sleepMicros = static_cast<useconds_t>(waitSeconds * 1000000.0);
  usleep(sleepMicros);
}

bool Robots_Agent::allow(const string& crawlUrl, URLNormalizer& normalizer, fetcher& theFetcher,
                         CrawlLogger& crawlLogger)
{

  URL parsed = normalizer.parseURL(crawlUrl);
  if (parsed.host.size() == 0)
  {
    return false;
  }

  const string origin = buildOrigin(parsed);
  bool shouldFetchRobots = false;
  policiesMu_.lock();
  {
    Robots_Rules* policy = getOrCreatePolicy(origin);
    if (!policy->initialized)
    {
      policy->initialized = true;
      shouldFetchRobots = true;
    }
  }
  policiesMu_.unlock();

  // Dont hold lock during network
  if (shouldFetchRobots)
  {
    string robotsUrl = origin;
    robotsUrl += "/robots.txt";

    auto result = theFetcher.fetch(robotsUrl);
    if (!result.hasValue())
    {
      string msg = "ROBOTS FETCH FAILED: ";
      msg += result.error().msg;
      crawlLogger.log(robotsUrl, msg.cstr());
    }
    else
    {
      fetchResponse resp = result.value();
      if (resp.statusCode == 200)
      {
        RobotsTxt* parsedRules = new RobotsTxt(reinterpret_cast<const Utf8*>(resp.body.cstr()), resp.body.size());
        policiesMu_.lock();
        Robots_Rules* policy = getOrCreatePolicy(origin);
        if (policy->rules == nullptr)
        {
          policy->rules = parsedRules;
          parsedRules = nullptr;
        }
        policiesMu_.unlock();
        if (parsedRules != nullptr)
        {
          delete parsedRules;
        }
      }
    }
  }

  
  bool allowed = true;
  double waitSeconds = 0.0;
  policiesMu_.lock();
  // crit section
  Robots_Rules* policy = getOrCreatePolicy(origin);
  if (policy->rules != nullptr)
  {
    int crawlDelay = 0;
    allowed = policy->rules->UrlAllowed(
        userAgent_, reinterpret_cast<const Utf8*>(crawlUrl.cstr()), &crawlDelay);
    if (crawlDelay > policy->crawlDelaySeconds)
    {
      policy->crawlDelaySeconds = crawlDelay;
    }
  }

  if (allowed)
  {
    double appliedDelaySeconds = static_cast<double>(policy->crawlDelaySeconds);
    if (appliedDelaySeconds > 0.3)
    {
      appliedDelaySeconds = 0.3;
    }
    if (appliedDelaySeconds < 0.0)
    {
      appliedDelaySeconds = 0.0;
    }

    const double now = nowSeconds();
    const double earliestAllowed = policy->lastRequestTime + appliedDelaySeconds;
    if (earliestAllowed > now)
    {
      waitSeconds = earliestAllowed - now;
    }

    // Reserve this request slot while lock is held to avoid per-origin races.
    policy->lastRequestTime = now + waitSeconds;
  }
  

  policiesMu_.unlock();

  if (!allowed)
  {
    crawlLogger.log(crawlUrl, "SKIP, ROBOTS DISALLOW");
    return false;
  }
  // dont lock hold during sleep
  enforceCrawlDelay(waitSeconds);
  return true;
}

void Robots_Agent::clearPolicies()
{
  policiesMu_.lock();
  for (size_t i = 0; i < policies_.size(); ++i)
  {
    delete policies_[i].rules;
    policies_[i].rules = nullptr;
  }
  policiesMu_.unlock();
}

} // namespace crawler
} // namespace avengine
