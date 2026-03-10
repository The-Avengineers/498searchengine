#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/crawler/domain_filter.h>

using namespace avengine;
using namespace avengine::crawler;

TEST_CASE("domainFilter - construction and destruction do not crash")
{
  domainFilter f;
  (void)f;
}

TEST_CASE("domainFilter - isBlocked returns false for standard HTTP URL")
{
  domainFilter f;
  CHECK(f.isBlocked("http://google.com/") == false);
}

