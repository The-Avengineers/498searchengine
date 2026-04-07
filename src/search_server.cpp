#include <avengine/avengine_lib/condition_variable.h>
#include <avengine/avengine_lib/lock_guard.h>
#include <avengine/avengine_lib/mutex.h>
#include <avengine/codec.h>
#include <avengine/node.h>
#include <avengine/search/search_common.h>
#include <avengine/search/search_service.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace avengine
{
namespace
{

enum class ServerRole
{
  Coordinator,
  Worker,
};

struct SearchSource
{
  bool IsPeer = false;
  size_t PeerIndex = 0;
  search::SearchResponse Response;
};

struct SearchServerState
{
  explicit SearchServerState(const string& indexDirectory) : Service(indexDirectory) {}

  search::SearchService Service;
  mutex ServiceMutex;
  mutex MailboxMutex;
  condition_variable MailboxCV;
  ServerRole Role = ServerRole::Coordinator;
  size_t PeerCount = 1;
  uint64_t NextRequestId = 1;
  bool RequestActive = false;
  uint64_t ActiveRequestId = 0;
  size_t ReceivedReplies = 0;
  vector<uint8_t> ReplySeen;
  vector<search::SearchResponse> Replies;
};

static SearchServerState* gState = nullptr;

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

ServerRole ParseRole(const char* roleEnv)
{
  if (roleEnv && std::strcmp(roleEnv, "worker") == 0)
  {
    return ServerRole::Worker;
  }
  return ServerRole::Coordinator;
}

void AppendJsonString(string& out, const string& value)
{
  out.pushBack('"');
  for (size_t i = 0; i < value.size(); ++i)
  {
    // Keep the output valid JSON even if the title or URL contains control chars.
    if (static_cast<unsigned char>(value[i]) < 0x20)
    {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      const char hex[] = "0123456789abcdef";
      out.pushBack('\\');
      out.pushBack('u');
      out.pushBack('0');
      out.pushBack('0');
      out.pushBack(hex[(c >> 4) & 0xF]);
      out.pushBack(hex[c & 0xF]);
      continue;
    }

    if (value[i] == '"' || value[i] == '\\')
      out.pushBack('\\');
    out.pushBack(value[i]);
  }
  out.pushBack('"');
}

void AppendDouble(string& out, double value)
{
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  out += string(buffer);
}

void WriteSearchError(Writer& writer, const search::SearchError& error)
{
  writer.writeU64(static_cast<uint64_t>(error.Offset));
  writer.writeString(error.Message);
}

search::SearchError ReadSearchError(Reader& reader)
{
  search::SearchError error;
  error.Offset = static_cast<size_t>(reader.readU64());
  error.Message = reader.readString();
  return error;
}

void WriteSearchHit(Writer& writer, const search::SearchHit& hit)
{
  writer.writeString(hit.Url);
  writer.writeString(hit.Title);

  uint64_t scoreBits = 0;
  std::memcpy(&scoreBits, &hit.Score, sizeof(hit.Score));
  writer.writeU64(scoreBits);
}

search::SearchHit ReadSearchHit(Reader& reader)
{
  search::SearchHit hit;
  hit.Url = reader.readString();
  hit.Title = reader.readString();

  const uint64_t scoreBits = reader.readU64();
  std::memcpy(&hit.Score, &scoreBits, sizeof(hit.Score));
  return hit;
}

void WriteSearchResponse(Writer& writer, const search::SearchResponse& response)
{
  writer.writeString(response.Query);
  writer.writeU64(static_cast<uint64_t>(response.Page));
  writer.writeU64(static_cast<uint64_t>(response.Limit));
  writer.writeU64(static_cast<uint64_t>(response.TotalHits));
  writer.writeU64(static_cast<uint64_t>(response.TookMs));

  writer.writeU64(static_cast<uint64_t>(response.Errors.size()));
  for (size_t i = 0; i < response.Errors.size(); ++i)
    WriteSearchError(writer, response.Errors[i]);

  writer.writeU64(static_cast<uint64_t>(response.Results.size()));
  for (size_t i = 0; i < response.Results.size(); ++i)
    WriteSearchHit(writer, response.Results[i]);
}

search::SearchResponse ReadSearchResponse(Reader& reader)
{
  search::SearchResponse response;
  response.Query = reader.readString();
  response.Page = static_cast<size_t>(reader.readU64());
  response.Limit = static_cast<size_t>(reader.readU64());
  response.TotalHits = static_cast<size_t>(reader.readU64());
  response.TookMs = static_cast<size_t>(reader.readU64());

  const size_t errorCount = static_cast<size_t>(reader.readU64());
  for (size_t i = 0; i < errorCount; ++i)
    response.Errors.pushBack(ReadSearchError(reader));

  const size_t resultCount = static_cast<size_t>(reader.readU64());
  for (size_t i = 0; i < resultCount; ++i)
    response.Results.pushBack(ReadSearchHit(reader));

  return response;
}

vector<uint8_t> EncodeSearchQueryRequest(uint64_t requestId, const string& query, size_t limit)
{
  Writer writer;
  writer.writeU64(requestId);
  writer.writeString(query);
  writer.writeU64(static_cast<uint64_t>(limit));
  return writer.release();
}

bool DecodeSearchQueryRequest(const vector<uint8_t>& payload, uint64_t* requestId, string* query,
                              size_t* limit)
{
  if (payload.size() < 16u || requestId == nullptr || query == nullptr || limit == nullptr)
  {
    return false;
  }

  Reader reader(payload.begin(), payload.size());
  *requestId = reader.readU64();
  *query = reader.readString();
  *limit = static_cast<size_t>(reader.readU64());
  return true;
}

void PrefixAndAppendErrors(vector<search::SearchError>& outErrors, size_t peerIndex,
                           const vector<search::SearchError>& errors)
{
  for (size_t i = 0; i < errors.size(); ++i)
  {
    search::SearchError prefixed;
    prefixed.Offset = peerIndex;
    prefixed.Message = string("peer #");
    search::AppendUnsigned(prefixed.Message, peerIndex);
    prefixed.Message += string(": ");
    prefixed.Message += errors[i].Message;
    outErrors.pushBack(prefixed);
  }
}

void AppendTransportError(vector<search::SearchError>& outErrors, size_t peerIndex,
                          const char* message)
{
  search::SearchError error;
  error.Offset = peerIndex;
  error.Message = string("peer #");
  search::AppendUnsigned(error.Message, peerIndex);
  error.Message += string(": ");
  error.Message += string(message);
  outErrors.pushBack(error);
}

bool ContainsUrl(const vector<search::SearchHit>& hits, const string& url)
{
  for (size_t i = 0; i < hits.size(); ++i)
  {
    if (hits[i].Url == url)
      return true;
  }
  return false;
}

size_t ComputeWindowStart(size_t page, size_t limit)
{
  if (page <= 1 || limit == 0)
  {
    return 0;
  }

  const size_t maxSize = static_cast<size_t>(-1);
  if (page - 1 > maxSize / limit)
  {
    return maxSize;
  }

  return (page - 1) * limit;
}

size_t ComputeOverfetchLimit(size_t page, size_t limit)
{
  const size_t pageNormalized = page == 0 ? 1 : page;
  const size_t limitNormalized = limit == 0 ? 10 : limit;
  const size_t maxSize = static_cast<size_t>(-1);
  if (limitNormalized != 0 && pageNormalized > (maxSize - 20u) / limitNormalized)
  {
    return maxSize;
  }
  return pageNormalized * limitNormalized + 20u;
}

search::SearchResponse MergeResponses(const string& query, size_t page, size_t limit,
                                      const vector<SearchSource>& sources,
                                      const vector<search::SearchError>& transportErrors,
                                      size_t tookMs)
{
  search::SearchResponse response;
  response.Query = query;
  response.Page = page == 0 ? 1 : page;
  response.Limit = limit == 0 ? 10 : limit;
  response.TookMs = tookMs;

  vector<search::SearchHit> candidates;
  for (size_t i = 0; i < sources.size(); ++i)
  {
    const SearchSource& source = sources[i];
    response.TotalHits += source.Response.TotalHits;

    if (source.IsPeer)
      PrefixAndAppendErrors(response.Errors, source.PeerIndex, source.Response.Errors);
    else
    {
      for (size_t e = 0; e < source.Response.Errors.size(); ++e)
        response.Errors.pushBack(source.Response.Errors[e]);
    }

    for (size_t r = 0; r < source.Response.Results.size(); ++r)
      candidates.pushBack(source.Response.Results[r]);
  }

  for (size_t i = 0; i < transportErrors.size(); ++i)
    response.Errors.pushBack(transportErrors[i]);

  candidates.sort(
      [](const search::SearchHit& lhs, const search::SearchHit& rhs)
      {
        if (lhs.Score != rhs.Score)
          return lhs.Score > rhs.Score;
        return lhs.Url < rhs.Url;
      });

  vector<search::SearchHit> uniqueHits;
  for (size_t i = 0; i < candidates.size(); ++i)
  {
    if (!ContainsUrl(uniqueHits, candidates[i].Url))
      uniqueHits.pushBack(candidates[i]);
  }

  const size_t start = ComputeWindowStart(response.Page, response.Limit);
  const size_t end = start > static_cast<size_t>(-1) - response.Limit ? static_cast<size_t>(-1)
                                                                      : start + response.Limit;
  for (size_t i = start; i < uniqueHits.size() && i < end; ++i)
    response.Results.pushBack(uniqueHits[i]);

  return response;
}

void BeginDistributedRequest(SearchServerState& state, size_t peerCount, uint64_t* requestId)
{
  avengine::lock_guard<mutex> guard(state.MailboxMutex);
  state.RequestActive = true;
  state.ActiveRequestId = state.NextRequestId++;
  state.ReceivedReplies = 0;
  state.ReplySeen = vector<uint8_t>(peerCount, 0);
  state.Replies = vector<search::SearchResponse>(peerCount);
  *requestId = state.ActiveRequestId;
}

bool StoreReply(SearchServerState& state, size_t peerIndex, uint64_t requestId,
                const search::SearchResponse& response)
{
  avengine::lock_guard<mutex> guard(state.MailboxMutex);
  if (!state.RequestActive || state.ActiveRequestId != requestId ||
      peerIndex >= state.ReplySeen.size() || state.ReplySeen[peerIndex] != 0)
  {
    return false;
  }

  state.ReplySeen[peerIndex] = 1;
  state.Replies[peerIndex] = response;
  ++state.ReceivedReplies;
  state.MailboxCV.broadcast();
  return true;
}

bool WaitForReplies(SearchServerState& state, size_t expectedReplies, size_t timeoutMs)
{
  if (expectedReplies == 0)
    return true;

  const auto startedAt = std::chrono::steady_clock::now();
  state.MailboxMutex.lock();
  while (state.RequestActive && state.ReceivedReplies < expectedReplies)
  {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt);
    const size_t elapsedMs = static_cast<size_t>(elapsed.count());
    if (elapsedMs >= timeoutMs)
      break;

    const size_t remainingMs = timeoutMs - elapsedMs;
    if (!state.MailboxCV.waitFor(state.MailboxMutex, remainingMs))
      break;
  }
  state.MailboxMutex.unlock();
  return true;
}

void HandleSearchQuery(size_t peerIndex, const vector<uint8_t>& payload)
{
  if (gState == nullptr)
    return;

  uint64_t requestId = 0;
  string query;
  size_t limit = 10;
  if (!DecodeSearchQueryRequest(payload, &requestId, &query, &limit))
  {
    fprintf(stderr, "[search_query] failed to decode request from peer #%zu\n", peerIndex);
    return;
  }

  search::SearchResponse response;
  {
    avengine::lock_guard<mutex> guard(gState->ServiceMutex);
    response = gState->Service.SearchPage(query, 1, limit);
  }

  Writer writer;
  writer.writeU64(requestId);
  WriteSearchResponse(writer, response);
  vector<uint8_t> reply = writer.release();
  if (sendMessage(peerIndex, MessageType::SEARCH_REPLY, reply))
  {
    fprintf(stderr, "[search_query] failed to send reply to peer #%zu\n", peerIndex);
  }
}

void HandleSearchReply(size_t peerIndex, const vector<uint8_t>& payload)
{
  if (gState == nullptr)
    return;

  Reader reader(payload.begin(), payload.size());
  const uint64_t requestId = reader.readU64();
  search::SearchResponse response = ReadSearchResponse(reader);

  if (!StoreReply(*gState, peerIndex, requestId, response))
    return;
}

search::SearchResponse PerformCoordinatorSearch(SearchServerState& state, const string& query,
                                                size_t page, size_t limit)
{
  const auto startedAt = std::chrono::steady_clock::now();
  search::SearchResponse response;
  response.Query = query;
  response.Page = page == 0 ? 1 : page;
  response.Limit = limit == 0 ? 10 : limit;

  constraint_solver::ConstraintSolver validator;
  const auto parsed = validator.ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    for (size_t i = 0; i < parsed.Errors.size(); ++i)
      response.Errors.pushBack({parsed.Errors[i].Offset, parsed.Errors[i].Message});
    constraint_solver::DestroyQueryTree(parsed.Root);
    response.TookMs = static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - startedAt)
                                              .count());
    return response;
  }
  constraint_solver::DestroyQueryTree(parsed.Root);

  const size_t timeoutMs = state.Service.GetQueryExecutionTimeThresholdMs();
  const size_t internalLimit = ComputeOverfetchLimit(response.Page, response.Limit);

  uint64_t requestId = 0;
  BeginDistributedRequest(state, state.PeerCount, &requestId);

  vector<uint8_t> sentPeers(state.PeerCount, 0);
  size_t expectedReplies = 0;
  vector<uint8_t> request = EncodeSearchQueryRequest(requestId, query, internalLimit);
  for (size_t peer = 0; peer < state.PeerCount; ++peer)
  {
    if (peer == localNodeId)
      continue;

    if (sendMessage(peer, MessageType::SEARCH_QUERY, request))
    {
      sentPeers[peer] = 1;
      ++expectedReplies;
    }
    else
    {
      AppendTransportError(response.Errors, peer, "failed to send search request");
    }
  }

  search::SearchResponse localResponse;
  {
    avengine::lock_guard<mutex> guard(state.ServiceMutex);
    localResponse = state.Service.SearchPage(query, 1, internalLimit);
  }

  if (timeoutMs > 0)
    WaitForReplies(state, expectedReplies, timeoutMs);

  vector<SearchSource> sources;
  sources.pushBack({false, localNodeId, localResponse});

  vector<search::SearchError> transportErrors = response.Errors;
  response.Errors.clear();

  state.MailboxMutex.lock();
  for (size_t peer = 0; peer < state.PeerCount; ++peer)
  {
    if (!sentPeers[peer])
      continue;

    if (state.ReplySeen[peer] != 0)
    {
      sources.pushBack({true, peer, state.Replies[peer]});
      continue;
    }

    AppendTransportError(transportErrors, peer, "timed out waiting for search reply");
  }
  state.RequestActive = false;
  state.MailboxCV.broadcast();
  state.MailboxMutex.unlock();

  const size_t tookMs = static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - startedAt)
                                                .count());

  return MergeResponses(query, response.Page, response.Limit, sources, transportErrors, tookMs);
}

void AppendResultString(string& body, const search::SearchResponse& response)
{
  body += string("{\"query\":");
  AppendJsonString(body, response.Query);
  body += string(",\"page\":");
  search::AppendUnsigned(body, response.Page);
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
    body += string(",\"score\":");
    AppendDouble(body, response.Results[i].Score);
    body.pushBack('}');
  }
  body += string("]}");
}

void SendResponse(int clientFd, int statusCode, const char* statusText, const string& body)
{
  string headers("HTTP/1.1 ");
  search::AppendUnsigned(headers, static_cast<size_t>(statusCode));
  headers += string(" ");
  headers += string(statusText);
  headers += string("\r\nContent-Type: application/json");
  headers += string("\r\nAccess-Control-Allow-Origin: *");
  headers += string("\r\nAccess-Control-Allow-Methods: GET, OPTIONS");
  headers += string("\r\nAccess-Control-Allow-Headers: Accept, Content-Type");
  headers += string("\r\nContent-Length: ");
  search::AppendUnsigned(headers, body.size());
  headers += string("\r\nConnection: close\r\n\r\n");
  headers += body;
  send(clientFd, headers.cstr(), headers.size(), 0);
}

void HandleClient(int clientFd, SearchServerState& state)
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
  const bool isGet = StartsWith(request, "GET ");
  const bool isOptions = StartsWith(request, "OPTIONS ");

  if (methodEnd == string::npos || targetEnd == string::npos || methodEnd >= lineEnd ||
      targetEnd > lineEnd || (!isGet && !isOptions))
  {
    SendResponse(clientFd, 405, "Method Not Allowed",
                 "{\"query\":\"\",\"errors\":[{\"message\":\"GET or OPTIONS only\"}],"
                 "\"results\":[]}");
    return;
  }

  const string target = CopyRange(request, methodEnd + 1, targetEnd);
  if (!StartsWith(target, "/api/search"))
  {
    SendResponse(clientFd, 404, "Not Found",
                 "{\"query\":\"\",\"errors\":[{\"message\":\"not found\"}],\"results\":[]}");
    return;
  }

  if (isOptions)
  {
    SendResponse(clientFd, 204, "No Content", string());
    return;
  }

  string query;
  size_t page = 1;
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
      else if (key == string("page") && value.size() != 0)
        page = static_cast<size_t>(strtoul(value.cstr(), nullptr, 10));

      if (next == target.size())
        break;
      cursor = next + 1;
    }
  }

  search::SearchResponse response;
  if (state.Role == ServerRole::Coordinator)
  {
    response = PerformCoordinatorSearch(state, query, page, limit);
  }
  else
  {
    avengine::lock_guard<mutex> guard(state.ServiceMutex);
    response = state.Service.SearchPage(query, page, limit);
  }

  string body;
  AppendResultString(body, response);
  SendResponse(clientFd, 200, "OK", body);
}

} // namespace
} // namespace avengine

using namespace avengine;

int main(int argc, char** argv)
{
  signal(SIGPIPE, SIG_IGN);

  const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
  const avengine::string argvIndexDirectory =
      argc > 2 ? avengine::string(argv[2]) : avengine::string(".");

  const char* indexDirEnv = getenv("INDEX_DIR");
  const avengine::string indexDirectory =
      indexDirEnv ? avengine::string(indexDirEnv) : argvIndexDirectory;

  const char* roleEnv = getenv("SEARCH_ROLE");
  const ServerRole role = ParseRole(roleEnv);

  const char* nodeIdEnv = getenv("NODE_ID");
  const char* numPeersEnv = getenv("NUM_PEERS");
  avengine::localNodeId = nodeIdEnv ? static_cast<size_t>(atoi(nodeIdEnv)) : 0;
  avengine::numPeers = numPeersEnv ? static_cast<size_t>(atoi(numPeersEnv)) : 1;
  if (avengine::numPeers < 1)
    avengine::numPeers = 1;

  SearchServerState state(indexDirectory);
  state.Role = role;
  state.PeerCount = numPeers;
  gState = &state;

  registerMessageHandler(MessageType::SEARCH_QUERY, HandleSearchQuery);
  registerMessageHandler(MessageType::SEARCH_REPLY, HandleSearchReply);

  if (numPeers > 1)
  {
    runNode(static_cast<uint64_t>(localNodeId), numPeers);
  }

  if (role == ServerRole::Worker)
  {
    std::cout << "search_server running in worker mode using index dir " << indexDirectory.cstr()
              << "\n";
    for (;;)
      pause();
  }

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
    HandleClient(clientFd, state);
    close(clientFd);
  }
}
