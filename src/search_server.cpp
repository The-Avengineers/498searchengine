#include <avengine/search/search_common.h>
#include <avengine/search/search_service.h>

#include <arpa/inet.h>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

namespace avengine
{
namespace
{

size_t FindChar(const string& text, char needle, size_t start = 0)
{
  for (size_t i = start; i < text.size(); ++i)
  {
    if (text[i] == needle)
      return i;
  }
  return string::npos;
}

string CopyRange(const string& text, size_t start, size_t end)
{
  string out;
  for (size_t i = start; i < end && i < text.size(); ++i)
    out.pushBack(text[i]);
  return out;
}

bool StartsWith(const string& text, const char* prefix)
{
  for (size_t i = 0; prefix[i] != '\0'; ++i)
  {
    if (i >= text.size() || text[i] != prefix[i])
      return false;
  }
  return true;
}

string DecodeQueryValue(const string& encoded)
{
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

void AppendJsonString(string& out, const string& value)
{
  out.pushBack('"');
  for (size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] == '"' || value[i] == '\\')
      out.pushBack('\\');
    out.pushBack(value[i]);
  }
  out.pushBack('"');
}

string MakeJsonResponse(const search::SearchResponse& response)
{
  string body("{\"query\":");
  AppendJsonString(body, response.Query);
  body += string(",\"limit\":");
  search::AppendUnsigned(body, response.Limit);
  body += string(",\"total_hits\":");
  search::AppendUnsigned(body, response.TotalHits);
  body += string(",\"took_ms\":");
  search::AppendUnsigned(body, response.TookMs);
  body += string(",\"errors\":[");
  for (size_t i = 0; i < response.Errors.size(); ++i)
  {
    if (i != 0)
      body.pushBack(',');
    body += string("{\"offset\":");
    search::AppendUnsigned(body, response.Errors[i].Offset);
    body += string(",\"message\":");
    AppendJsonString(body, response.Errors[i].Message);
    body.pushBack('}');
  }
  body += string("],\"results\":[");
  for (size_t i = 0; i < response.Results.size(); ++i)
  {
    if (i != 0)
      body.pushBack(',');
    body += string("{\"url\":");
    AppendJsonString(body, response.Results[i].Url);
    body += string(",\"title\":");
    AppendJsonString(body, response.Results[i].Title);
    body += string(",\"score\":1}");
  }
  body += string("]}");
  return body;
}

void SendResponse(int clientFd, int statusCode, const char* statusText, const string& body)
{
  string headers("HTTP/1.1 ");
  search::AppendUnsigned(headers, static_cast<size_t>(statusCode));
  headers += string(" ");
  headers += string(statusText);
  headers += string("\r\nContent-Type: application/json\r\nContent-Length: ");
  search::AppendUnsigned(headers, body.size());
  headers += string("\r\nConnection: close\r\n\r\n");
  headers += body;
  send(clientFd, headers.cstr(), headers.size(), 0);
}

void HandleClient(int clientFd, const search::SearchService& service)
{
  char buffer[4096];
  const ssize_t bytes = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
  if (bytes <= 0)
    return;

  buffer[bytes] = '\0';
  const string request(buffer);
  const size_t lineEnd = request.find("\r\n");
  const size_t methodEnd = FindChar(request, ' ');
  const size_t targetEnd = FindChar(request, ' ', methodEnd == string::npos ? 0 : methodEnd + 1);

  if (methodEnd == string::npos || targetEnd == string::npos || methodEnd >= lineEnd ||
      targetEnd > lineEnd || !StartsWith(request, "GET "))
  {
    SendResponse(clientFd, 405, "Method Not Allowed",
                 string("{\"query\":\"\",\"errors\":[{\"message\":\"GET only\"}],\"results\":[]}"));
    return;
  }

  const string target = CopyRange(request, methodEnd + 1, targetEnd);
  if (!StartsWith(target, "/api/search"))
  {
    SendResponse(
        clientFd, 404, "Not Found",
        string("{\"query\":\"\",\"errors\":[{\"message\":\"not found\"}],\"results\":[]}"));
    return;
  }

  string query;
  size_t limit = 10;
  const size_t queryStart = FindChar(target, '?');
  if (queryStart != string::npos)
  {
    size_t cursor = queryStart + 1;
    while (cursor <= target.size())
    {
      size_t next = FindChar(target, '&', cursor);
      if (next == string::npos)
        next = target.size();

      const string param = CopyRange(target, cursor, next);
      const size_t equals = FindChar(param, '=');
      const string key = equals == string::npos ? param : CopyRange(param, 0, equals);
      const string value =
          equals == string::npos ? string() : CopyRange(param, equals + 1, param.size());

      if (key == string("q"))
        query = DecodeQueryValue(value);
      else if (key == string("limit") && value.size() != 0)
        limit = search::ClampSearchLimit(static_cast<size_t>(strtoul(value.cstr(), nullptr, 10)));

      if (next == target.size())
        break;
      cursor = next + 1;
    }
  }

  const search::SearchResponse response = service.Search(query, limit);
  SendResponse(clientFd, 200, "OK", MakeJsonResponse(response));
}

} // namespace
} // namespace avengine

int main(int argc, char** argv)
{
  const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
  const avengine::string indexDirectory = argc > 2 ? avengine::string(argv[2]) : avengine::string(".");
  const avengine::search::SearchService service(indexDirectory);

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

  std::cout << "search_server listening on port " << port << " using index dir "
            << indexDirectory.cstr() << "\n";
  for (;;)
  {
    const int clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0)
      continue;
    // One request at a time is enough here. The slow part we care about next is query + shard
    // work, not fancy socket plumbing.
    avengine::HandleClient(clientFd, service);
    close(clientFd);
  }
}
