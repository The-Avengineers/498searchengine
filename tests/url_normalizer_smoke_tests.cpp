#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/crawler/url_normalizer.h>

using namespace avengine;
using namespace avengine::crawler;

// ---------------------------------------------------------------------------
// parseURL
// ---------------------------------------------------------------------------

TEST_CASE("parseURL - simple HTTP URL")
{
  URLNormalizer n;
  URL u = n.parseURL("http://www.google.com/search");
  CHECK(u.protocol == "http");
  CHECK(u.host == "www.google.com");
  CHECK(u.port == "");
  CHECK(u.path == "/search");
}

TEST_CASE("parseURL - HTTPS with non-default port")
{
  URLNormalizer n;
  URL u = n.parseURL("https://example.com:8443/page");
  CHECK(u.protocol == "https");
  CHECK(u.host == "example.com");
  CHECK(u.port == "8443");
  CHECK(u.path == "/page");
}

TEST_CASE("parseURL - default HTTP port 80 is omitted")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com:80/foo");
  CHECK(u.port == "");
}

TEST_CASE("parseURL - default HTTPS port 443 is omitted")
{
  URLNormalizer n;
  URL u = n.parseURL("https://example.com:443/foo");
  CHECK(u.port == "");
}

TEST_CASE("parseURL - no path defaults to /")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com");
  CHECK(u.path == "/");
}

TEST_CASE("parseURL - trailing slash on path is removed")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com/foo/");
  CHECK(u.path == "/foo");
}

TEST_CASE("parseURL - fragment is stripped")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com/page#section");
  CHECK(u.path == "/page");
}

TEST_CASE("parseURL - uppercase protocol and host are lowercased")
{
  URLNormalizer n;
  URL u = n.parseURL("HTTP://EXAMPLE.COM/path");
  CHECK(u.protocol == "http");
  CHECK(u.host == "example.com");
}

TEST_CASE("parseURL - path with multiple segments")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com/a/b/c");
  CHECK(u.path == "/a/b/c");
}

// ---------------------------------------------------------------------------
// resolveDotSegments
// ---------------------------------------------------------------------------

TEST_CASE("resolveDotSegments - single dot is removed")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/foo/./bar") == "/foo/bar");
}

TEST_CASE("resolveDotSegments - double dot goes up one level")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/foo/../bar") == "/bar");
}

TEST_CASE("resolveDotSegments - multiple dots resolve correctly")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/foo/bar/../../baz") == "/baz");
}

TEST_CASE("resolveDotSegments - empty string returns /")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("") == "/");
}

TEST_CASE("resolveDotSegments - cannot go above root")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/../..") == "/");
}

TEST_CASE("resolveDotSegments - plain path is unchanged")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/foo/bar") == "/foo/bar");
}

TEST_CASE("resolveDotSegments - trailing dot segment")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/foo/bar/.") == "/foo/bar");
}

// ---------------------------------------------------------------------------
// normalizeIndividual
// ---------------------------------------------------------------------------

TEST_CASE("normalizeIndividual - lowercases protocol and host")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("HTTP://EXAMPLE.COM/path") == "http://example.com/path");
}

TEST_CASE("normalizeIndividual - strips default HTTP port")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com:80/path") == "http://example.com/path");
}

TEST_CASE("normalizeIndividual - strips default HTTPS port")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("https://example.com:443/path") == "https://example.com/path");
}

TEST_CASE("normalizeIndividual - removes trailing slash from path")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com/foo/") == "http://example.com/foo");
}

TEST_CASE("normalizeIndividual - resolves dot segments")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com/a/../b") == "http://example.com/b");
}

TEST_CASE("normalizeIndividual - preserves non-default port")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com:8080/path") == "http://example.com:8080/path");
}

// ---------------------------------------------------------------------------
// normalize (base + extension)
// ---------------------------------------------------------------------------

TEST_CASE("normalize - absolute URL extension is normalized directly")
{
  URLNormalizer n;
  string base = "http://example.com/page";
  string ext = "https://other.com/stuff";
  CHECK(n.normalize(base, ext) == "https://other.com/stuff");
}

TEST_CASE("normalize - protocol-relative URL inherits base protocol")
{
  URLNormalizer n;
  string base = "https://example.com/page";
  string ext = "//other.com/stuff";
  CHECK(n.normalize(base, ext) == "https://other.com/stuff");
}

TEST_CASE("normalize - absolute path uses base host and protocol")
{
  URLNormalizer n;
  string base = "http://example.com/some/path";
  string ext = "/newpath";
  CHECK(n.normalize(base, ext) == "http://example.com/newpath");
}

TEST_CASE("normalize - relative path merges with base directory")
{
  URLNormalizer n;
  string base = "http://example.com/dir/page.html";
  string ext = "other.html";
  CHECK(n.normalize(base, ext) == "http://example.com/dir/other.html");
}

TEST_CASE("normalize - relative path with dot-dot resolves against base")
{
  URLNormalizer n;
  string base = "http://example.com/a/b/page.html";
  string ext = "../other.html";
  CHECK(n.normalize(base, ext) == "http://example.com/a/other.html");
}

// ---------------------------------------------------------------------------
// parseURL - additional edge cases
// ---------------------------------------------------------------------------

TEST_CASE("parseURL - query string is preserved in path")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com/search?q=hello");
  CHECK(u.path == "/search?q=hello");
}

TEST_CASE("parseURL - fragment stripped but query preserved")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com/search?q=hello#section");
  CHECK(u.path == "/search?q=hello");
}

TEST_CASE("parseURL - path case is preserved (only host and protocol are lowercased)")
{
  URLNormalizer n;
  URL u = n.parseURL("http://EXAMPLE.COM/PATH/File.html");
  CHECK(u.host == "example.com");
  CHECK(u.path == "/PATH/File.html");
}

TEST_CASE("parseURL - root trailing slash gives path /")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com/");
  CHECK(u.path == "/");
}

TEST_CASE("parseURL - IPv4 address as host")
{
  URLNormalizer n;
  URL u = n.parseURL("http://192.168.1.1/path");
  CHECK(u.host == "192.168.1.1");
  CHECK(u.port == "");
  CHECK(u.path == "/path");
}

TEST_CASE("parseURL - IPv4 address with explicit port")
{
  URLNormalizer n;
  URL u = n.parseURL("http://192.168.1.1:8080/path");
  CHECK(u.host == "192.168.1.1");
  CHECK(u.port == "8080");
}

TEST_CASE("parseURL - non-default port with no path defaults path to /")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com:8080");
  CHECK(u.host == "example.com");
  CHECK(u.port == "8080");
  CHECK(u.path == "/");
}

TEST_CASE("parseURL - protocol-less host/path defaults protocol to http")
{
  URLNormalizer n;
  URL u = n.parseURL("example.com/path");
  CHECK(u.protocol == "http");
  CHECK(u.host == "example.com");
  CHECK(u.port == "");
  CHECK(u.path == "/path");
}

TEST_CASE("parseURL - protocol-less uppercase host is lowercased")
{
  URLNormalizer n;
  URL u = n.parseURL("EXAMPLE.COM/Path");
  CHECK(u.protocol == "http");
  CHECK(u.host == "example.com");
  CHECK(u.path == "/Path");
}

TEST_CASE("parseURL - custom protocol is preserved and lowercased")
{
  URLNormalizer n;
  URL u = n.parseURL("FTP://EXAMPLE.COM/file");
  CHECK(u.protocol == "ftp");
  CHECK(u.host == "example.com");
  CHECK(u.path == "/file");
}

TEST_CASE("parseURL - fragment-only suffix on host gives root path")
{
  URLNormalizer n;
  URL u = n.parseURL("http://example.com#frag");
  CHECK(u.path == "/");
}

// ---------------------------------------------------------------------------
// resolveDotSegments - additional edge cases
// ---------------------------------------------------------------------------

TEST_CASE("resolveDotSegments - single slash returns /")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/") == "/");
}

TEST_CASE("resolveDotSegments - double slash collapses")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("//foo") == "/foo");
}

TEST_CASE("resolveDotSegments - double-dot past root clamps to /")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/a/b/../../../c") == "/c");
}

TEST_CASE("resolveDotSegments - multiple sequential single dots removed")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/foo/./bar/./baz") == "/foo/bar/baz");
}

TEST_CASE("resolveDotSegments - all dot segments collapse to /")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/./././") == "/");
}

TEST_CASE("resolveDotSegments - repeated slashes collapse")
{
  URLNormalizer n;
  CHECK(n.resolveDotSegments("/a//b///c") == "/a/b/c");
}

// ---------------------------------------------------------------------------
// normalizeIndividual - additional edge cases
// ---------------------------------------------------------------------------

TEST_CASE("normalizeIndividual - query string is preserved")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com/search?q=test") ==
        "http://example.com/search?q=test");
}

TEST_CASE("normalizeIndividual - fragment stripped when query is present")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com/page?q=1#anchor") ==
        "http://example.com/page?q=1");
}

TEST_CASE("normalizeIndividual - path case is preserved")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("HTTP://EXAMPLE.COM/Path/File.html") ==
        "http://example.com/Path/File.html");
}

TEST_CASE("normalizeIndividual - root URL preserved")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com/") == "http://example.com/");
}

TEST_CASE("normalizeIndividual - no path normalizes to root slash")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("http://example.com") == "http://example.com/");
}

TEST_CASE("normalizeIndividual - protocol-less URL becomes explicit http")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("Example.com/Some/Path") == "http://example.com/Some/Path");
}

TEST_CASE("normalizeIndividual - custom protocol is preserved")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("FTP://Example.com/File.txt") == "ftp://example.com/File.txt");
}

TEST_CASE("normalizeIndividual - malformed triple-slash absolute URL is repaired")
{
  URLNormalizer n;
  CHECK(n.normalizeIndividual("https:///www.google.com") == "https://www.google.com/");
}

// ---------------------------------------------------------------------------
// normalize - additional edge cases
// ---------------------------------------------------------------------------

TEST_CASE("normalize - base with query string, relative extension ignores query in merge")
{
  URLNormalizer n;
  string base = "http://example.com/dir/page.html?q=test";
  string ext = "other.html";
  CHECK(n.normalize(base, ext) == "http://example.com/dir/other.html");
}

TEST_CASE("normalize - relative extension with double-dot past base root clamps")
{
  URLNormalizer n;
  string base = "http://example.com/page.html";
  string ext = "../../other.html";
  CHECK(n.normalize(base, ext) == "http://example.com/other.html");
}

TEST_CASE("normalize - empty extension resolves to base directory")
{
  URLNormalizer n;
  string base = "http://example.com/dir/page.html";
  string ext = "";
  CHECK(n.normalize(base, ext) == "http://example.com/dir");
}

TEST_CASE("normalize - absolute path extension preserves base port")
{
  URLNormalizer n;
  string base = "http://example.com:8080/old/path";
  string ext = "/new/path";
  CHECK(n.normalize(base, ext) == "http://example.com:8080/new/path");
}

TEST_CASE("normalize - relative extension from root-level base")
{
  URLNormalizer n;
  string base = "http://example.com/";
  string ext = "page.html";
  CHECK(n.normalize(base, ext) == "http://example.com/page.html");
}

TEST_CASE("normalize - relative extension with dot prefix")
{
  URLNormalizer n;
  string base = "http://example.com/dir/page.html";
  string ext = "./child.html";
  CHECK(n.normalize(base, ext) == "http://example.com/dir/child.html");
}

TEST_CASE("normalize - protocol-relative extension keeps base protocol, drops base port")
{
  URLNormalizer n;
  string base = "http://example.com:8080/dir/page.html";
  string ext = "//other.example/path";
  CHECK(n.normalize(base, ext) == "http://other.example/path");
}

TEST_CASE("normalize - protocol-relative extension with extra slash is repaired")
{
  URLNormalizer n;
  string base = "https://example.com/dir/page.html";
  string ext = "///www.google.com";
  CHECK(n.normalize(base, ext) == "https://www.google.com/");
}

TEST_CASE("normalize - base with no explicit path and relative extension")
{
  URLNormalizer n;
  string base = "http://example.com";
  string ext = "child";
  CHECK(n.normalize(base, ext) == "http://example.com/child");
}

TEST_CASE("normalize - absolute extension normalizes host/protocol case and default port")
{
  URLNormalizer n;
  string base = "http://example.com/a";
  string ext = "HTTPS://Other.COM:443/Path";
  CHECK(n.normalize(base, ext) == "https://other.com/Path");
}
