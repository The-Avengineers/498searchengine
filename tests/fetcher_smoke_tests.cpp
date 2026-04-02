#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/crawler/fetcher.h>
#include <cstring>
#include <zlib.h>

using namespace avengine;
using namespace avengine::crawler;

// Helper: compress a string to gzip format so we can build test inputs
// for decompressGzip without hard-coding binary blobs.
static string makeGzip(const char* data, size_t len)
{
  string result;
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  // windowBits = 15+16 for gzip format
  deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
  zs.avail_in = static_cast<uInt>(len);
  char outbuf[4096];
  int ret;
  do
  {
    zs.next_out = reinterpret_cast<Bytef*>(outbuf);
    zs.avail_out = sizeof(outbuf);
    ret = deflate(&zs, Z_FINISH);
    size_t have = sizeof(outbuf) - zs.avail_out;
    for (size_t i = 0; i < have; ++i)
      result.pushBack(outbuf[i]);
  } while (ret == Z_OK);
  deflateEnd(&zs);
  return result;
}

static string toHex(size_t value)
{
  if (value == 0)
  {
    return "0";
  }

  char rev[32];
  size_t len = 0;
  while (value > 0)
  {
    size_t digit = value & 0xF;
    rev[len++] = static_cast<char>((digit < 10) ? ('0' + digit) : ('a' + (digit - 10)));
    value >>= 4;
  }

  string result;
  for (size_t i = 0; i < len; ++i)
  {
    result.pushBack(rev[len - 1 - i]);
  }
  return result;
}

static string makeChunked(const string& payload)
{
  string result;
  result += toHex(payload.size());
  result += "\r\n";
  result += payload;
  result += "\r\n0\r\n\r\n";
  return result;
}

// Note: parseResponse() and buildRequest() are private and cannot be tested
// directly. Public API tests focus on construction and fetch() error paths
// that do not require a live network connection.

TEST_CASE("fetcher - construction and destruction do not crash")
{
  fetcher f;
  (void)f;
}

TEST_CASE("fetcher - fetch with non-existent domain returns error")
{
  // .invalid TLD is guaranteed by RFC 2606 to never resolve
  fetcher f;
  auto result = f.fetch("http://this-host-does-not-exist.invalid/");
  CHECK(result.hasValue() == false);
}

TEST_CASE("fetcher - fetch with non-existent HTTPS domain returns error")
{
  fetcher f;
  auto result = f.fetch("https://this-host-does-not-exist.invalid/path");
  CHECK(result.hasValue() == false);
}

TEST_CASE("fetcher - fetch with clearly bogus hostname returns error")
{
  fetcher f;
  auto result = f.fetch("http://not-a-real-host-xyzzy-12345.invalid/");
  CHECK(result.hasValue() == false);
}

TEST_CASE("fetcher - expected error result has a non-empty message")
{
  fetcher f;
  auto result = f.fetch("http://no-such-host.invalid/");
  CHECK(result.hasValue() == false);
  CHECK(result.error().msg.size() > 0);
}

// ---------------------------------------------------------------------------
// decodeChunked
// ---------------------------------------------------------------------------

TEST_CASE("decodeChunked - two chunks concatenated")
{
  string body = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "Hello World");
}

TEST_CASE("decodeChunked - terminal chunk only yields empty string")
{
  string body = "0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "");
}

TEST_CASE("decodeChunked - single chunk")
{
  // "Hello, World!" = 13 bytes = 0xd
  string body = "d\r\nHello, World!\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "Hello, World!");
}

TEST_CASE("decodeChunked - uppercase hex size digit")
{
  string body = "D\r\nHello, World!\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "Hello, World!");
}

TEST_CASE("decodeChunked - chunk extension after semicolon is ignored")
{
  string body = "5;name=value\r\nHello\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "Hello");
}

TEST_CASE("decodeChunked - three chunks")
{
  // "foo" (3) + "bar" (3) + "baz" (3)
  string body = "3\r\nfoo\r\n3\r\nbar\r\n3\r\nbaz\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "foobarbaz");
}

TEST_CASE("decodeChunked - multi-digit hex size")
{
  // 16 bytes = 0x10
  string body = "10\r\n0123456789abcdef\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "0123456789abcdef");
}

TEST_CASE("decodeChunked - ignores trailers after terminal chunk")
{
  string body = "3\r\nabc\r\n0\r\nSome-Trailer: x\r\nOther: y\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "abc");
}

TEST_CASE("decodeChunked - malformed truncated chunk returns partial payload")
{
  string body = "5\r\nab";
  CHECK(fetcher::decodeChunked(body) == "ab");
}

TEST_CASE("decodeChunked - invalid hex chunk size yields empty output")
{
  string body = "g\r\nHello\r\n0\r\n\r\n";
  CHECK(fetcher::decodeChunked(body) == "");
}

// ---------------------------------------------------------------------------
// decompressGzip
// ---------------------------------------------------------------------------

TEST_CASE("decompressGzip - roundtrip short string")
{
  const char* text = "Hello, compressed world!";
  string compressed = makeGzip(text, strlen(text));
  string decompressed = fetcher::decompressGzip(compressed);
  CHECK(decompressed == "Hello, compressed world!");
}

TEST_CASE("decompressGzip - roundtrip empty string")
{
  string compressed = makeGzip("", 0);
  string decompressed = fetcher::decompressGzip(compressed);
  CHECK(decompressed == "");
}

TEST_CASE("decompressGzip - roundtrip longer string")
{
  // Build a longer string with repeated content
  string original;
  for (int i = 0; i < 200; ++i)
    original.pushBack(static_cast<char>('a' + (i % 26)));

  string compressed = makeGzip(original.cstr(), original.size());
  string decompressed = fetcher::decompressGzip(compressed);
  CHECK(decompressed == original);
}

TEST_CASE("decompressGzip - roundtrip payload larger than output buffer")
{
  // Larger than the internal 4096-byte inflate chunk buffer.
  string original;
  for (int i = 0; i < 12000; ++i)
  {
    original.pushBack(static_cast<char>('A' + (i % 26)));
  }

  string compressed = makeGzip(original.cstr(), original.size());
  string decompressed = fetcher::decompressGzip(compressed);
  CHECK(decompressed == original);
}

TEST_CASE("decompressGzip - corrupted input returns empty string")
{
  string garbage = "this is not gzip data at all";
  string result = fetcher::decompressGzip(garbage);
  CHECK(result == "");
}

TEST_CASE("chunked + gzip pipeline - decodeChunked then decompressGzip roundtrip")
{
  const char* text = "Hello from a chunked+gzip payload";
  string compressed = makeGzip(text, strlen(text));
  string chunked = makeChunked(compressed);

  string decoded = fetcher::decodeChunked(chunked);
  string decompressed = fetcher::decompressGzip(decoded);
  CHECK(decompressed == text);
}

TEST_CASE("isHtmlContentType - text/html is accepted")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n";
  CHECK(fetcher::isHtmlContentType(headers) == true);
}

TEST_CASE("isHtmlContentType - application/xhtml+xml is accepted")
{
  string headers = "HTTP/1.1 200 OK\r\ncontent-type: application/xhtml+xml\r\n\r\n";
  CHECK(fetcher::isHtmlContentType(headers) == true);
}

TEST_CASE("isHtmlContentType - non-html content is rejected")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
  CHECK(fetcher::isHtmlContentType(headers) == false);
}

TEST_CASE("isHtmlContentType - case-insensitive and whitespace tolerant")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Type:\tTEXT/HTML ; charset=utf-8\r\n\r\n";
  CHECK(fetcher::isHtmlContentType(headers) == true);
}

TEST_CASE("isHtmlContentType - xhtml with charset parameter is accepted")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Type: application/xhtml+xml; charset=UTF-8\r\n\r\n";
  CHECK(fetcher::isHtmlContentType(headers) == true);
}

TEST_CASE("isHtmlContentType - html token in unrelated header does not count")
{
  string headers = "HTTP/1.1 200 OK\r\nX-Note: text/html\r\nContent-Type: text/plain\r\n\r\n";
  CHECK(fetcher::isHtmlContentType(headers) == false);
}

TEST_CASE("isEnglishContentLanguage - missing header is accepted")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
  CHECK(fetcher::isEnglishContentLanguage(headers) == true);
}

TEST_CASE("isEnglishContentLanguage - english language tag is accepted")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Language: en-US\r\n\r\n";
  CHECK(fetcher::isEnglishContentLanguage(headers) == true);
}

TEST_CASE("isEnglishContentLanguage - english among multiple tags is accepted")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Language: fr, en\r\n\r\n";
  CHECK(fetcher::isEnglishContentLanguage(headers) == true);
}

TEST_CASE("isEnglishContentLanguage - non-english tags are rejected")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Language: fr-CA, de\r\n\r\n";
  CHECK(fetcher::isEnglishContentLanguage(headers) == false);
}

TEST_CASE("isEnglishHtmlLang - missing html lang is accepted")
{
  string body = "<!doctype html><html><head></head><body>Hello</body></html>";
  CHECK(fetcher::isEnglishHtmlLang(body) == true);
}

TEST_CASE("isEnglishHtmlLang - english html lang is accepted")
{
  string body = "<html lang=\"en-US\"><body>Hello</body></html>";
  CHECK(fetcher::isEnglishHtmlLang(body) == true);
}

TEST_CASE("isEnglishHtmlLang - non-english html lang is rejected")
{
  string body = "<html lang='fr'><body>Bonjour</body></html>";
  CHECK(fetcher::isEnglishHtmlLang(body) == false);
}

TEST_CASE("isEnglishHtmlLang - xml:lang is recognized")
{
  string body = "<html xml:lang=\"fr\"><body>Bonjour</body></html>";
  CHECK(fetcher::isEnglishHtmlLang(body) == false);
}

TEST_CASE("isEnglishHtmlLang - xml:lang is recognized and english accepted")
{
  string body = "<html xml:lang=\"en\"><body>Bonjour</body></html>";
  CHECK(fetcher::isEnglishHtmlLang(body) == false);
}

TEST_CASE("isEnglishOkStatus - accepts literal 200 OK")
{
  string headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
  CHECK(fetcher::isEnglishOkStatus(headers) == true);
}

TEST_CASE("isEnglishOkStatus - accepts case-insensitive OK with whitespace")
{
  string headers = "HTTP/1.1 200   Ok \t\r\nContent-Type: text/html\r\n\r\n";
  CHECK(fetcher::isEnglishOkStatus(headers) == true);
}

TEST_CASE("isEnglishOkStatus - rejects localized reason phrase for 200")
{
  string headers = "HTTP/1.1 200 D'accord\r\nContent-Type: text/html\r\n\r\n";
  CHECK(fetcher::isEnglishOkStatus(headers) == false);
}

TEST_CASE("isEnglishOkStatus - rejects non-200 status lines")
{
  string headers = "HTTP/1.1 204 No Content\r\nContent-Type: text/html\r\n\r\n";
  CHECK(fetcher::isEnglishOkStatus(headers) == false);
}
