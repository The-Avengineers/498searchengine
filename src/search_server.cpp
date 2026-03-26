#include <avengine/avengine_lib/string.h>

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace avengine
{
namespace
{

size_t findChar(const string& text, char needle, size_t start = 0)
{
  for (size_t i = start; i < text.size(); ++i)
  {
    if (text[i] == needle)
      return i;
  }
  return string::npos;
}

string copyRange(const string& text, size_t start, size_t end)
{
  string out;
  for (size_t i = start; i < end && i < text.size(); ++i)
    out.pushBack(text[i]);
  return out;
}

bool startsWith(const string& text, const char* prefix)
{
  for (size_t i = 0; prefix[i] != '\0'; ++i)
  {
    if (i >= text.size() || text[i] != prefix[i])
      return false;
  }
  return true;
}

void appendUnsigned(string& out, size_t value)
{
  char digits[32];
  size_t count = 0;
  do
  {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value > 0);

  while (count > 0)
    out.pushBack(digits[--count]);
}

string decodeQueryValue(const string& encoded)
{
  // This is just enough decoding for the search box path right now.
  string decoded;
  for (size_t i = 0; i < encoded.size(); ++i)
  {
    if (encoded[i] == '+')
    {
      decoded.pushBack(' ');
      continue;
    }

    if (encoded[i] == '%' && i + 2 < encoded.size())
    {
      char hex[3] = {encoded[i + 1], encoded[i + 2], '\0'};
      decoded.pushBack(static_cast<char>(strtol(hex, nullptr, 16)));
      i += 2;
      continue;
    }

    decoded.pushBack(encoded[i]);
  }
  return decoded;
}

bool isBlank(const string& value)
{
  for (size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] != ' ' && value[i] != '\t' && value[i] != '\r' && value[i] != '\n')
      return false;
  }
  return true;
}

void appendJsonString(string& out, const string& value)
{
  out.pushBack('"');
  for (size_t i = 0; i < value.size(); ++i)
  {
    const char c = value[i];
    if (c == '"' || c == '\\')
      out.pushBack('\\');
    out.pushBack(c);
  }
  out.pushBack('"');
}

string makeJsonResponse(const string& query, size_t limit, bool ok)
{
  string body;
  if (ok)
  {
    body += string("{\"query\":");
    appendJsonString(body, query);
    body += string(",\"limit\":");
    appendUnsigned(body, limit);
    body += string(",\"total_hits\":0,\"took_ms\":0,\"results\":[],\"phase\":\"transport-only\"}");
  }
  else
  {
    body += string("{\"query\":\"\",\"errors\":[{\"message\":\"missing query\"}],\"results\":[]}");
  }
  return body;
}

void sendResponse(int clientFd, int statusCode, const char* statusText, const string& body)
{
  string headers("HTTP/1.1 ");
  appendUnsigned(headers, static_cast<size_t>(statusCode));
  headers += string(" ");
  headers += string(statusText);
  headers += string("\r\nContent-Type: application/json\r\nContent-Length: ");
  appendUnsigned(headers, body.size());
  headers += string("\r\nConnection: close\r\n\r\n");
  headers += body;
  send(clientFd, headers.cstr(), headers.size(), 0);
}

void handleClient(int clientFd)
{
  char buffer[4096];
  const ssize_t bytes = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0)
    return;

  buffer[bytes] = '\0';
  const string request(buffer);
  // parse the request line and ignore the rest for now.
  const size_t lineEnd = request.find("\r\n");
  const size_t methodEnd = findChar(request, ' ');
  const size_t targetEnd = findChar(request, ' ', methodEnd == string::npos ? 0 : methodEnd + 1);

  if (methodEnd == string::npos || targetEnd == string::npos ||
      methodEnd >= lineEnd || targetEnd > lineEnd || !startsWith(request, "GET "))
  {
    sendResponse(clientFd, 405, "Method Not Allowed",
                 string("{\"query\":\"\",\"errors\":[{\"message\":\"GET only\"}],\"results\":[]}"));
    return;
  }

  const string target = copyRange(request, methodEnd + 1, targetEnd);
  if (!startsWith(target, "/api/search"))
  {
    // Frontend files land in the next slice. For now this is API only.
    sendResponse(clientFd, 404, "Not Found",
                 string("{\"query\":\"\",\"errors\":[{\"message\":\"not found\"}],\"results\":[]}"));
    return;
  }

  string query;
  size_t limit = 10;
  const size_t queryStart = findChar(target, '?');
  if (queryStart != string::npos)
  {
    size_t cursor = queryStart + 1;
    while (cursor <= target.size())
    {
      size_t next = findChar(target, '&', cursor);
      if (next == string::npos)
        next = target.size();

      const string param = copyRange(target, cursor, next);
      const size_t equals = findChar(param, '=');
      const string key = equals == string::npos ? param : copyRange(param, 0, equals);
      const string value = equals == string::npos ? string() : copyRange(param, equals + 1, param.size());

      if (key == "q")
        query = decodeQueryValue(value);
      else if (key == "limit" && value.size() != 0)
      {
        limit = static_cast<size_t>(strtoul(value.cstr(), nullptr, 10));
        // We only ever want a tiny batch here, so clamp hard and keep moving.
        if (limit == 0 || limit > 10)
          limit = 10;
      }

      if (next == target.size())
        break;
      cursor = next + 1;
    }
  }

  if (query.size() == 0 || isBlank(query))
  {
    sendResponse(clientFd, 400, "Bad Request", makeJsonResponse(query, limit, false));
    return;
  }

  sendResponse(clientFd, 200, "OK", makeJsonResponse(query, limit, true));
}

} // namespace
} // namespace avengine

int main(int argc, char** argv)
{
  const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
  const int serverFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (serverFd < 0)
    return 1;

  int reuse = 1;
  setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(serverFd, 8) != 0)
  {
    std::cerr << "search_server failed to bind port " << port << "\n";
    close(serverFd);
    return 1;
  }

  std::cout << "search_server listening on port " << port << "\n";
  for (;;)
  {
    const int clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0)
      continue;
    // One request at a time is fine for this batch. We can add threads later if we need them.
    avengine::handleClient(clientFd);
    close(clientFd);
  }
}
