#pragma once

#include "../avengine_lib/expected.h"
#include "../avengine_lib/string.h"


namespace avengine
{
namespace crawler
{

struct fetchResponse
{
  string body;
  string headers;
  int statusCode;
};

class fetcher
{
public:
  fetcher();
  ~fetcher();

  // Requires: url is a valid URL string
  // Modifies: nothing persistent
  // Returns: fetchResponse on success, error on failure.
  //          Always attempts HTTPS.
  expected<fetchResponse, error> fetch(const string& url);

  // Requires: body is a valid HTTP chunked-transfer-encoded string
  // Modifies: nothing
  // Returns: decoded body with chunk framing removed
  static string decodeChunked(const string& body);

  // Requires: compressed is gzip-compressed data
  // Modifies: nothing
  // Returns: decompressed data, or empty string on failure
  static string decompressGzip(const string& compressed);

  // Requires: response headers from an HTTP response
  // Modifies: nothing
  // Returns: true if Content-Type indicates HTML
  static bool isHtmlContentType(const string& headers);

private:
  void* sslCtx_;

  // Requires: host, port (as string), path
  // Modifies: nothing persistent
  // Returns: fetchResponse on success via plain TCP
  expected<fetchResponse, error> fetchHttp(const string& host, const string& port,
                                           const string& path);

  // Requires: host, port, path
  // Modifies: nothing persistent
  // Returns: fetchResponse on success via SSL/TLS
  expected<fetchResponse, error> fetchHttps(const string& host, const string& port,
                                            const string& path);

  // Requires: host and path
  // Modifies: nothing
  // Returns: HTTP GET request string
  string buildRequest(const string& host, const string& path);

  // Requires: raw HTTP response (headers + body)
  // Modifies: nothing
  // Returns: parsed fetchResponse with status code, headers, body
  fetchResponse parseResponse(const string& raw);
};

} // namespace crawler
} // namespace avengine
