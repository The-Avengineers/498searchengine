#include "../../include/avengine/crawler/robots_helper.h"
#include "../../include/avengine/crawler/RobotsTxt.h"
#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/logger.h"

#include <time.h>
#include <unistd.h>

namespace avengine
{
namespace crawler
{

Robots_Agent::Robots_Rules::Robots_Rules()
    : rules(nullptr), crawlDelaySeconds(0), lastRequestTime(0), initialized(false)
{}

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

void Robots_Agent::ensurePolicyLoaded(Robots_Rules& policy, fetcher& theFetcher,
                                      CrawlLogger& crawlLogger)
{
  if (policy.initialized)
  {
    return;
  }
  policy.initialized = true;

  string robotsUrl = policy.origin;
  robotsUrl += "/robots.txt";

  auto result = theFetcher.fetch(robotsUrl);
  if (!result.hasValue())
  {
    string msg = "ROBOTS FETCH FAILED: ";
    msg += result.error().msg;
    crawlLogger.log(robotsUrl, msg.cstr());
    return;
  }

  fetchResponse resp = result.value();
  if (resp.statusCode != 200)
  {
    return;
  }

  policy.rules = new RobotsTxt(reinterpret_cast<const Utf8*>(resp.body.cstr()), resp.body.size());
}

void Robots_Agent::enforceCrawlDelay(const Robots_Rules& policy) const
{
  if (policy.crawlDelaySeconds <= 0 || policy.lastRequestTime == 0)
  {
    return;
  }

  const time_t now = time(nullptr);
  const time_t waitUntil = policy.lastRequestTime + policy.crawlDelaySeconds;
  if (now < waitUntil)
  {
    const unsigned int waitSeconds = static_cast<unsigned int>(waitUntil - now);
    sleep(waitSeconds);
  }
}

bool Robots_Agent::allow(const string& crawlUrl, URLNormalizer& normalizer, fetcher& theFetcher,
                         CrawlLogger& crawlLogger)
{
  URL parsed = normalizer.parseURL(crawlUrl);
  if (parsed.host.size() == 0)
  {
    return false;
  }

  Robots_Rules* policy = getOrCreatePolicy(buildOrigin(parsed));
  ensurePolicyLoaded(*policy, theFetcher, crawlLogger);

  if (policy->rules != nullptr)
  {
    int crawlDelay = 0;
    const bool allowed = policy->rules->UrlAllowed(
        userAgent_, reinterpret_cast<const Utf8*>(crawlUrl.cstr()), &crawlDelay);
    if (crawlDelay > policy->crawlDelaySeconds)
    {
      policy->crawlDelaySeconds = crawlDelay;
    }
    if (!allowed)
    {
      crawlLogger.log(crawlUrl, "SKIP, ROBOTS DISALLOW");
      return false;
    }
  }

  enforceCrawlDelay(*policy);
  policy->lastRequestTime = time(nullptr);
  return true;
}

void Robots_Agent::clearPolicies()
{
  for (size_t i = 0; i < policies_.size(); ++i)
  {
    delete policies_[i].rules;
    policies_[i].rules = nullptr;
  }
}

} // namespace crawler
} // namespace avengine
