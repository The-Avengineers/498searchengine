#include "../../include/avengine/crawler/fetcher.h"
#include "../../../include/avengine_lib/LinuxGetUrlSslStarter/LinuxGetUrl.cpp"
#include "../../include/avengine/crawler/url_normalizer.h"
#include "../../include/avengine/expected/expected.h"
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/ssl.h>

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

fetchResponse fetcher::parseResponse(const string& raw)
{
  fetchResponse resp;
  resp.statusCode = 0;

  // Find \r\n\r\n boundary
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

  // Parse status code from first line: "HTTP/1.1 200 OK"
  // Find the first space
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

  return resp;
}

expected<fetchResponse, error> fetcher::fetch(const string& url)
{
  URLNormalizer normalizer;
  string normalizedUrl = normalizer.normalize(url);
  URL parsed = parseURL(normalizedUrl);

  // Use default port if not specified
  string port = parsed.port;
  if (port == "")
  {
    port = (parsed.protocol == "https") ? "443" : "80";
  }

  if (parsed.protocol == "https")
  {
    return fetchHttps(parsed.host, port, parsed.path);
  }

  return fetchHttp(parsed.host, port, parsed.path);
}

string fetcher::buildRequest(const string& host, const string& path)
{
  const char* p1 = "GET ";
  const char* p2 = " HTTP/1.1\r\nHost: ";
  const char* p3 = "\r\nUser-Agent: " USER_AGENT "\r\nAccept: */*\r\nAccept-Encoding: "
                   "identity\r\nConnection: close\r\n\r\n";

  string req;
  req.reserve(strlen(p1) + strlen(p2) + strlen(p3) + host.size() + path.size());
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

  int con_res = connect(fd, address->ai_addr, address->ai_addrlen);

  if (con_res == -1)
  {
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

  int con_res = connect(fd, address->ai_addr, address->ai_addrlen);

  if (con_res == -1)
  {
    return expected<fetchResponse, error>::error({"connection failed"});
  }

  SSL* ssl = SSL_new(static_cast<SSL_CTX*>(sslCtx_));
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
