#include "../../include/avengine/crawler/fetcher.h"
#include "../../include/avengine/crawler/url_normalizer.h"
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <zlib.h>

namespace avengine
{
namespace crawler
{

fetcher::fetcher()
{
  SSL_library_init();
  sslCtx_ = SSL_CTX_new(SSLv23_method());
}

fetcher::~fetcher()
{
  if (sslCtx_)
  {
    SSL_CTX_free(static_cast<SSL_CTX*>(sslCtx_));
  }
}

#define USER_AGENT "Avengineers-Crawler/1.0 marundel@umich.edu"
#define MAX_REDIRECTS 5
#define CONNECT_TIMEOUT_MS 8000

static inline char lowerAscii(char c)
{
  if (c >= 'A' && c <= 'Z')
  {
    return static_cast<char>(c + 32);
  }
  return c;
}

static bool isRedirectStatus(int code)
{
  return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

static bool connectWithTimeout(int fd, const struct sockaddr* addr, socklen_t addrLen, int timeoutMs,
                               int* socketError)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
  {
    *socketError = errno;
    return false;
  }
  // set non blocking
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    *socketError = errno;
    return false;
  }

  // try to connect
  int connectResult = connect(fd, addr, addrLen);
  if (connectResult == 0)
  {
    // success
    fcntl(fd, F_SETFL, flags);
    *socketError = 0;
    return true;
  }

  if (errno != EINPROGRESS)
  {
    // Error
    *socketError = errno;
    fcntl(fd, F_SETFL, flags);
    return false;
  }

  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLOUT;
  pfd.revents = 0;
  // poll the socket for 8 seconds
  int pollResult = poll(&pfd, 1, timeoutMs);
  if (pollResult <= 0)
  {
    if (pollResult == 0) 
    {
      *socketError = ETIMEDOUT;
    }
    else {
      *socketError = errno;
    }
    fcntl(fd, F_SETFL, flags);
    return false;
  }

  int sockErr = 0;
  socklen_t soLen = sizeof(sockErr);
  //
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &soLen) == -1)
  {
    *socketError = errno;
    fcntl(fd, F_SETFL, flags);
    return false;
  }

  // connection failed
  if (sockErr != 0)
  {
    *socketError = sockErr;
    fcntl(fd, F_SETFL, flags);
    return false;
  }

  // restore flags
  if (fcntl(fd, F_SETFL, flags) == -1)
  {
    *socketError = errno;
    return false;
  }

  // connection worked
  *socketError = 0;
  return true;
}

// Pull a single header value (trimmed) from the raw header block.
static string getHeaderValue(const string& headers, const char* headerName)
{
  const size_t nameLen = strlen(headerName);

  for (size_t i = 0; i + nameLen <= headers.size(); ++i)
  {
    if (i != 0 && headers[i - 1] != '\n')
    {
      continue;
    }

    bool nameMatch = true;
    for (size_t j = 0; j < nameLen; ++j)
    {
      if (lowerAscii(headers[i + j]) != lowerAscii(headerName[j]))
      {
        nameMatch = false;
        break;
      }
    }
    if (!nameMatch)
    {
      continue;
    }

    size_t valueStart = i + nameLen;
    while (valueStart < headers.size() &&
           (headers[valueStart] == ' ' || headers[valueStart] == '\t'))
    {
      ++valueStart;
    }

    size_t valueEnd = valueStart;
    while (valueEnd < headers.size() && headers[valueEnd] != '\r' && headers[valueEnd] != '\n')
    {
      ++valueEnd;
    }
    while (valueEnd > valueStart && (headers[valueEnd - 1] == ' ' || headers[valueEnd - 1] == '\t'))
    {
      --valueEnd;
    }

    string value;
    for (size_t k = valueStart; k < valueEnd; ++k)
    {
      value.pushBack(headers[k]);
    }
    return value;
  }

  return string();
}

// Check whether a header value contains a token
// Example: headerValueContainsToken(headers, "content-type:", "text/html")
static bool headerValueContainsToken(const string& headers, const char* headerName,
                                     const char* token)
{
  const size_t nameLen = strlen(headerName);
  const size_t tokenLen = strlen(token);

  for (size_t i = 0; i + nameLen <= headers.size(); ++i)
  {
    if (i != 0 && headers[i - 1] != '\n')
    {
      continue;
    }

    bool nameMatch = true;
    for (size_t j = 0; j < nameLen; ++j)
    {
      if (lowerAscii(headers[i + j]) != lowerAscii(headerName[j]))
      {
        nameMatch = false;
        break;
      }
    }
    if (!nameMatch)
    {
      continue;
    }

    size_t valuePos = i + nameLen;
    while (valuePos < headers.size() && (headers[valuePos] == ' ' || headers[valuePos] == '\t'))
    {
      ++valuePos;
    }

    size_t lineEnd = valuePos;
    while (lineEnd < headers.size() && headers[lineEnd] != '\r' && headers[lineEnd] != '\n')
    {
      ++lineEnd;
    }

    for (size_t k = valuePos; k + tokenLen <= lineEnd; ++k)
    {
      bool tokenMatch = true;
      for (size_t t = 0; t < tokenLen; ++t)
      {
        if (lowerAscii(headers[k + t]) != lowerAscii(token[t]))
        {
          tokenMatch = false;
          break;
        }
      }
      if (tokenMatch)
      {
        return true;
      }
    }
  }

  return false;
}

fetchResponse fetcher::parseResponse(const string& raw)
{
  fetchResponse resp;
  resp.statusCode = 0;

  // Split response into headers/body
  size_t boundary = raw.size();
  for (size_t i = 3; i < raw.size(); ++i)
  {
    if (raw[i - 3] == '\r' && raw[i - 2] == '\n' && raw[i - 1] == '\r' && raw[i] == '\n')
    {
      boundary = i - 3;
      break;
    }
  }

  // Headers = everything before \r\n\r\n
  for (size_t i = 0; i < boundary; ++i)
  {
    resp.headers.pushBack(raw[i]);
  }

  // Body = everything after \r\n\r\n
  size_t bodyStart = boundary + 4;
  for (size_t i = bodyStart; i < raw.size(); ++i)
  {
    resp.body.pushBack(raw[i]);
  }

  // Parse status code from start line ("HTTP/1.1 200 OK").
  size_t spacePos = 0;
  for (size_t i = 0; i < resp.headers.size(); ++i)
  {
    if (resp.headers[i] == ' ')
    {
      spacePos = i;
      break;
    }
  }

  // Read the 3-digit code after the space
  if (spacePos > 0 && spacePos + 3 < resp.headers.size())
  {
    int code = 0;
    for (size_t i = spacePos + 1; i < spacePos + 4; ++i)
    {
      char c = resp.headers[i];
      if (c >= '0' && c <= '9')
      {
        code = code * 10 + (c - '0');
      }
    }
    resp.statusCode = code;
  }

  // HTTP wire format is chunked first then optional content coding.
  if (headerValueContainsToken(resp.headers, "transfer-encoding:", "chunked"))
  {
    resp.body = decodeChunked(resp.body);
  }

  // Decompress gzip content encoding
  if (headerValueContainsToken(resp.headers, "content-encoding:", "gzip"))
  {
    resp.body = decompressGzip(resp.body);
  }

  return resp;
}

string fetcher::decodeChunked(const string& body)
{
  string result;
  size_t i = 0;
  while (i < body.size())
  {
    // Find end of chunk-size line (\r\n)
    size_t lineEnd = i;
    while (lineEnd + 1 < body.size() && !(body[lineEnd] == '\r' && body[lineEnd + 1] == '\n'))
    {
      ++lineEnd;
    }

    if (lineEnd + 1 >= body.size())
      break;

    size_t chunkSize = 0;
    for (size_t j = i; j < lineEnd; ++j)
    {
      char c = body[j];
      if (c == ';')
        break;
      chunkSize <<= 4;
      if (c >= '0' && c <= '9')
        chunkSize |= static_cast<size_t>(c - '0');
      else if (c >= 'a' && c <= 'f')
        chunkSize |= static_cast<size_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        chunkSize |= static_cast<size_t>(c - 'A' + 10);
    }

    i = lineEnd + 2; // skip past \r\n

    if (chunkSize == 0)
      break; // terminal chunk

    for (size_t j = 0; j < chunkSize && i + j < body.size(); ++j)
    {
      result.pushBack(body[i + j]);
    }

    i += chunkSize + 2; // skip chunk data + trailing \r\n
  }
  return result;
}

string fetcher::decompressGzip(const string& compressed)
{
  string result;
  z_stream zs;
  memset(&zs, 0, sizeof(zs));

  // windowBits = 15+16 tells zlib to expect gzip format
  if (inflateInit2(&zs, 15 + 16) != Z_OK)
    return result;

  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.cstr()));
  zs.avail_in = static_cast<uInt>(compressed.size());

  char outbuf[4096];
  int ret;
  do
  {
    zs.next_out = reinterpret_cast<Bytef*>(outbuf);
    zs.avail_out = sizeof(outbuf);
    ret = inflate(&zs, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END)
      break;
    size_t have = sizeof(outbuf) - zs.avail_out;
    for (size_t i = 0; i < have; ++i)
    {
      result.pushBack(outbuf[i]);
    }
  } while (ret != Z_STREAM_END && zs.avail_in > 0);

  inflateEnd(&zs);
  return result;
}

bool fetcher::isHtmlContentType(const string& headers)
{
  return headerValueContainsToken(headers, "content-type:", "text/html") ||
         headerValueContainsToken(headers, "content-type:", "application/xhtml+xml");
}

expected<fetchResponse, error> fetcher::fetch(const string& url)
{
  if (!sslCtx_)
  {
    return expected<fetchResponse, error>::error({"SSL context not initialized"});
  }

  URLNormalizer normalizer;
  string currentUrl = normalizer.normalizeIndividual(url);

  // follow up to max redirects
  for (int redirects = 0; redirects <= MAX_REDIRECTS; ++redirects)
  {
    URL parsed = normalizer.parseURL(currentUrl);

    // upgrade to https
    if (parsed.protocol != "https")
    {
      parsed.protocol = "https";
      // Avoid trying TLS on port 80 after an HTTP->HTTPS upgrade.
      if (parsed.port == "" || parsed.port == "80")
      {
        parsed.port = "";
      }
    }

    currentUrl = normalizer.URLtoString(parsed);

    string port = parsed.port;
    if (port == "")
    {
      port = "443";
    }

    if (parsed.host.size() == 0)
    {
      return expected<fetchResponse, error>::error({"invalid URL host"});
    }

    auto result = fetchHttps(parsed.host, port, parsed.path);
    if (!result.hasValue())
    {
      return result;
    }

    fetchResponse resp = result.value();
    if (!isRedirectStatus(resp.statusCode))
    {
      return expected<fetchResponse, error>::ok(resp);
    }

    string location = getHeaderValue(resp.headers, "location:");
    if (location.size() == 0)
    {
      return expected<fetchResponse, error>::error({"redirect without location"});
    }

    currentUrl = normalizer.normalize(currentUrl, location);
  }

  return expected<fetchResponse, error>::error({"too many redirects"});
}

string fetcher::buildRequest(const string& host, const string& path)
{
  const char* p1 = "GET ";
  const char* p2 = " HTTP/1.1\r\nHost: ";
  // Request gzip explicitly; parser handles gzip decode after optional chunk decoding.
  const char* p3 =
      "\r\nUser-Agent: " USER_AGENT "\r\nAccept: */*\r\nAccept-Encoding: gzip\r\nConnection: "
      "close\r\n\r\n";

  string req;
  req += p1;
  req += path;
  req += p2;
  req += host;
  req += p3;

  return req;
}

expected<fetchResponse, error> fetcher::fetchHttp(const string& host, const string& port,
                                                  const string& path)
{

  struct addrinfo *address, hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(host.cstr(), port.cstr(), &hints, &address) != 0)
  {
    return expected<fetchResponse, error>::error({"DNS resolution failed"});
  }

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd == -1)
  {
    freeaddrinfo(address);
    return expected<fetchResponse, error>::error({"socket creation failed"});
  }

  int socketError = 0;
  if (!connectWithTimeout(fd, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen),
                          CONNECT_TIMEOUT_MS, &socketError))
  {
    close(fd);
    freeaddrinfo(address);
    if (socketError == ETIMEDOUT)
    {
      return expected<fetchResponse, error>::error({"connection timeout"});
    }
    return expected<fetchResponse, error>::error({"connection failed"});
  }

  string request = buildRequest(host, path);
  ssize_t sent = send(fd, request.cstr(), request.size(), 0);
  if (sent == -1)
  {
    close(fd);
    freeaddrinfo(address);
    return expected<fetchResponse, error>::error({"send failed"});
  }

  // Receive response
  string raw;
  char buffer[4096];
  ssize_t bytes;
  while ((bytes = recv(fd, buffer, sizeof(buffer), 0)) > 0)
  {
    for (ssize_t i = 0; i < bytes; ++i)
    {
      raw.pushBack(buffer[i]);
    }
  }

  close(fd);
  freeaddrinfo(address);

  if (raw.size() == 0)
  {
    return expected<fetchResponse, error>::error({"empty response"});
  }

  return expected<fetchResponse, error>::ok(parseResponse(raw));
}

expected<fetchResponse, error> fetcher::fetchHttps(const string& host, const string& port,
                                                   const string& path)
{

  struct addrinfo *address, hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(host.cstr(), port.cstr(), &hints, &address) != 0)
  {
    return expected<fetchResponse, error>::error({"DNS resolution failed"});
  }

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd == -1)
  {
    freeaddrinfo(address);
    return expected<fetchResponse, error>::error({"socket creation failed"});
  }

  int socketError = 0;
  if (!connectWithTimeout(fd, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen),
                          CONNECT_TIMEOUT_MS, &socketError))
  {
    close(fd);
    freeaddrinfo(address);
    if (socketError == ETIMEDOUT)
    {
      return expected<fetchResponse, error>::error({"connection timeout"});
    }
    return expected<fetchResponse, error>::error({"connection failed"});
  }

  SSL* ssl = SSL_new(static_cast<SSL_CTX*>(sslCtx_));
  if (!ssl)
  {
    close(fd);
    freeaddrinfo(address);
    return expected<fetchResponse, error>::error({"SSL allocation failed"});
  }
  SSL_set_tlsext_host_name(ssl, host.cstr());
  SSL_set_fd(ssl, fd);

  if (SSL_connect(ssl) <= 0)
  {
    SSL_free(ssl);
    close(fd);
    freeaddrinfo(address);
    return expected<fetchResponse, error>::error({"SSL handshake failed"});
  }

  string request = buildRequest(host, path);
  ssize_t sent = SSL_write(ssl, request.cstr(), static_cast<int>(request.size()));
  if (sent <= 0)
  {
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    freeaddrinfo(address);
    return expected<fetchResponse, error>::error({"SSL send failed"});
  }

  // Receive response
  string raw;
  char buffer[4096];
  int bytes;
  while ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) > 0)
  {
    for (int i = 0; i < bytes; ++i)
    {
      raw.pushBack(buffer[i]);
    }
  }

  SSL_shutdown(ssl);
  SSL_free(ssl);
  close(fd);
  freeaddrinfo(address);

  if (raw.size() == 0)
  {
    return expected<fetchResponse, error>::error({"empty response"});
  }

  return expected<fetchResponse, error>::ok(parseResponse(raw));
}

} // namespace crawler
} // namespace avengine
