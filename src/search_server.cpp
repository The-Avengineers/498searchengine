#include <avengine/avengine_lib/condition_variable.h>
#include <avengine/avengine_lib/lock_guard.h>
#include <avengine/avengine_lib/mutex.h>
#include <avengine/avengine_lib/shared_mutex.h>
#include <avengine/codec.h>
#include <avengine/node.h>
#include <avengine/search/coordinator_merge.h>
#include <avengine/search/search_common.h>
#include <avengine/search/search_service.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
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

// Maximum number of distributed searches that may be in-flight simultaneously.
// Each in-flight request occupies one mailbox slot.
static const size_t kMaxConcurrentRequests = 16;

// Per-request mailbox: holds the replies collected from peer nodes for one
// distributed search and a condition variable the coordinator thread waits on.
struct RequestMailbox
{
  bool Active = false;
  uint64_t RequestId = 0;
  size_t ReceivedReplies = 0;
  vector<uint8_t> ReplySeen;
  vector<search::SearchResponse> Replies;
  condition_variable CV;
};

struct SearchServerState
{
  explicit SearchServerState(const string& indexDirectory) : Service(indexDirectory) {}

  search::SearchService Service;
  // Shared (readers-writer) lock: SearchPage/GetQueryExecutionTimeThresholdMs hold a
  // shared lock; SetQueryExecutionTimeThresholdMs holds the exclusive lock.
  SharedMutex ServiceMutex;
  mutex MailboxMutex;
  RequestMailbox Mailboxes[kMaxConcurrentRequests];
  uint64_t NextRequestId = 1;
  ServerRole Role = ServerRole::Coordinator;
  size_t PeerCount = 1;
};

static SearchServerState* gState = nullptr;

// ──────────────────────────────────────────────────────────────────────────────
// HTTP helpers
// ──────────────────────────────────────────────────────────────────────────────

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

bool EndsWith(const string& text, const char* suffix)
{
  size_t suffixLength = 0;
  while (suffix[suffixLength] != '\0')
    ++suffixLength;
  if (suffixLength > text.size())
    return false;

  const size_t start = text.size() - suffixLength;
  for (size_t i = 0; i < suffixLength; ++i)
  {
    if (text[start + i] != suffix[i])
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

string StripQueryString(const string& target)
{
  const size_t queryStart = FindChar(target, '?');
  if (queryStart == string::npos)
    return target;
  return CopyRange(target, 0, queryStart);
}

bool IsSafeStaticPath(const string& path)
{
  if (path.size() == 0 || path[0] != '/')
    return false;

  for (size_t i = 0; i < path.size(); ++i)
  {
    if (path[i] == '\\')
      return false;
    if (path[i] == '.' && i + 1 < path.size() && path[i + 1] == '.')
      return false;
  }
  return true;
}

string ResolveStaticPath(const string& target)
{
  const string path = StripQueryString(target);
  if (!IsSafeStaticPath(path))
    return string();

  if (path == string("/"))
    return string("frontend/index.html");

  string resolved("frontend");
  resolved += path;
  return resolved;
}

const char* GuessContentType(const string& path)
{
  if (EndsWith(path, ".html"))
    return "text/html; charset=utf-8";
  if (EndsWith(path, ".css"))
    return "text/css; charset=utf-8";
  if (EndsWith(path, ".js"))
    return "application/javascript; charset=utf-8";
  if (EndsWith(path, ".png"))
    return "image/png";
  if (EndsWith(path, ".jpg") || EndsWith(path, ".jpeg"))
    return "image/jpeg";
  if (EndsWith(path, ".svg"))
    return "image/svg+xml";
  return "application/octet-stream";
}

bool ReadFile(const string& path, string& out)
{
  std::ifstream in(path.cstr(), std::ios::binary);
  if (!in)
    return false;

  char ch = '\0';
  while (in.get(ch))
    out.pushBack(ch);
  return in.good() || in.eof();
}
ServerRole ParseRole(const char* roleEnv)
{
  if (roleEnv && std::strcmp(roleEnv, "worker") == 0)
    return ServerRole::Worker;
  return ServerRole::Coordinator;
}

void AppendJsonString(string& out, const string& value)
{
  out.pushBack('"');
  for (size_t i = 0; i < value.size(); ++i)
  {
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

// ──────────────────────────────────────────────────────────────────────────────
// Wire encoding for SEARCH_QUERY / SEARCH_REPLY messages
// ──────────────────────────────────────────────────────────────────────────────

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
    return false;

  Reader reader(payload.begin(), payload.size());
  *requestId = reader.readU64();
  *query = reader.readString();
  *limit = static_cast<size_t>(reader.readU64());
  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-request mailbox management
// ──────────────────────────────────────────────────────────────────────────────

// Allocates a free mailbox slot for a new distributed request.
// Returns false (and leaves *requestId/*mailboxIndex unchanged) if all slots are busy.
bool BeginDistributedRequest(SearchServerState& state, size_t peerCount, uint64_t* requestId,
                             size_t* mailboxIndex)
{
  avengine::lock_guard<mutex> guard(state.MailboxMutex);
  for (size_t i = 0; i < kMaxConcurrentRequests; ++i)
  {
    RequestMailbox& mb = state.Mailboxes[i];
    if (mb.Active)
      continue;

    mb.Active = true;
    mb.RequestId = state.NextRequestId++;
    mb.ReceivedReplies = 0;
    mb.ReplySeen = vector<uint8_t>(peerCount, 0);
    mb.Replies = vector<search::SearchResponse>(peerCount);
    *requestId = mb.RequestId;
    *mailboxIndex = i;
    return true;
  }
  return false;
}

// Stores a reply from peerIndex into the mailbox that matches requestId, then
// signals the coordinator thread waiting on that mailbox's condition variable.
bool StoreReply(SearchServerState& state, size_t peerIndex, uint64_t requestId,
                const search::SearchResponse& response)
{
  avengine::lock_guard<mutex> guard(state.MailboxMutex);
  for (size_t i = 0; i < kMaxConcurrentRequests; ++i)
  {
    RequestMailbox& mb = state.Mailboxes[i];
    if (!mb.Active || mb.RequestId != requestId)
      continue;
    if (peerIndex >= mb.ReplySeen.size() || mb.ReplySeen[peerIndex] != 0)
      return false;

    mb.ReplySeen[peerIndex] = 1;
    mb.Replies[peerIndex] = response;
    ++mb.ReceivedReplies;
    mb.CV.signal();
    return true;
  }
  return false;
}

// Waits up to timeoutMs for expectedReplies to arrive in the mailbox at mailboxIndex.
void WaitForReplies(SearchServerState& state, size_t mailboxIndex, size_t expectedReplies,
                    size_t timeoutMs)
{
  if (expectedReplies == 0)
    return;

  const auto startedAt = std::chrono::steady_clock::now();
  RequestMailbox& mb = state.Mailboxes[mailboxIndex];
  state.MailboxMutex.lock();
  while (mb.Active && mb.ReceivedReplies < expectedReplies)
  {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt);
    const size_t elapsedMs = static_cast<size_t>(elapsed.count());
    if (elapsedMs >= timeoutMs)
      break;

    const size_t remainingMs = timeoutMs - elapsedMs;
    if (!mb.CV.waitFor(state.MailboxMutex, remainingMs))
      break;
  }
  state.MailboxMutex.unlock();
}

// Frees the mailbox slot so it may be reused by the next request.
void EndDistributedRequest(SearchServerState& state, size_t mailboxIndex)
{
  avengine::lock_guard<mutex> guard(state.MailboxMutex);
  state.Mailboxes[mailboxIndex].Active = false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Message handlers
// ──────────────────────────────────────────────────────────────────────────────

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
    SharedLockGuard<SharedMutex> guard(gState->ServiceMutex);
    response = gState->Service.SearchPage(query, 1, limit);
  }

  Writer writer;
  writer.writeU64(requestId);
  WriteSearchResponse(writer, response);
  vector<uint8_t> reply = writer.release();
  if (sendMessage(peerIndex, MessageType::SEARCH_REPLY, reply))
    fprintf(stderr, "[search_query] failed to send reply to peer #%zu\n", peerIndex);
}

void HandleSearchReply(size_t peerIndex, const vector<uint8_t>& payload)
{
  if (gState == nullptr)
    return;

  Reader reader(payload.begin(), payload.size());
  const uint64_t requestId = reader.readU64();
  search::SearchResponse response = ReadSearchResponse(reader);

  StoreReply(*gState, peerIndex, requestId, response);
}

// ──────────────────────────────────────────────────────────────────────────────
// Coordinator search: fan-out, wait, merge
// ──────────────────────────────────────────────────────────────────────────────

search::SearchResponse PerformCoordinatorSearch(SearchServerState& state, const string& query,
                                                size_t page, size_t limit)
{
  const auto startedAt = std::chrono::steady_clock::now();

  const size_t normalizedPage = page == 0 ? 1 : page;
  const size_t normalizedLimit = limit == 0 ? 10 : limit;

  // Validate query syntax before touching the network.
  constraint_solver::ConstraintSolver validator;
  const auto parsed = validator.ParseQuery(query);
  if (!parsed.Errors.empty())
  {
    search::SearchResponse response;
    response.Query = query;
    response.Page = normalizedPage;
    response.Limit = normalizedLimit;
    for (size_t i = 0; i < parsed.Errors.size(); ++i)
      response.Errors.pushBack({parsed.Errors[i].Offset, parsed.Errors[i].Message});
    constraint_solver::DestroyQueryTree(parsed.Root);
    response.TookMs = static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - startedAt)
                                              .count());
    return response;
  }
  constraint_solver::DestroyQueryTree(parsed.Root);

  size_t timeoutMs;
  {
    SharedLockGuard<SharedMutex> guard(state.ServiceMutex);
    timeoutMs = state.Service.GetQueryExecutionTimeThresholdMs();
  }

  const size_t internalLimit = search::ComputeOverfetchLimit(normalizedPage, normalizedLimit);

  uint64_t requestId = 0;
  size_t mailboxIndex = 0;
  if (!BeginDistributedRequest(state, state.PeerCount, &requestId, &mailboxIndex))
  {
    search::SearchResponse response;
    response.Query = query;
    response.Page = normalizedPage;
    response.Limit = normalizedLimit;
    response.Errors.pushBack({0, string("server busy: too many concurrent searches")});
    response.TookMs = static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - startedAt)
                                              .count());
    return response;
  }

  // Fan out to all peers. Track which peers accepted the request.
  vector<uint8_t> sentPeers(state.PeerCount, 0);
  size_t expectedReplies = 0;
  vector<search::SearchError> transportErrors;
  vector<uint8_t> request = EncodeSearchQueryRequest(requestId, query, internalLimit);

  for (size_t peer = 0; peer < state.PeerCount; ++peer)
  {
    if (peer == localNodeId)
    {
      continue;
    }

    // sendMessage returns true on error, false on success.
    if (sendMessage(peer, MessageType::SEARCH_QUERY, request))
    {
      search::AppendTransportError(transportErrors, peer, "failed to send search request");
    }
    else
    {
      sentPeers[peer] = 1;
      ++expectedReplies;
    }
  }

  // Run the local shard search concurrently while replies are in-flight.
  search::SearchResponse localResponse;
  {
    SharedLockGuard<SharedMutex> guard(state.ServiceMutex);
    localResponse = state.Service.SearchPage(query, 1, internalLimit);
  }

  if (timeoutMs > 0)
    WaitForReplies(state, mailboxIndex, expectedReplies, timeoutMs);

  // Collect results from the mailbox before releasing the slot.
  vector<search::SearchSource> sources;
  sources.pushBack({false, localNodeId, localResponse});

  state.MailboxMutex.lock();
  RequestMailbox& mb = state.Mailboxes[mailboxIndex];
  for (size_t peer = 0; peer < state.PeerCount; ++peer)
  {
    if (!sentPeers[peer])
      continue;

    if (mb.ReplySeen[peer] != 0)
      sources.pushBack({true, peer, mb.Replies[peer]});
    else
      search::AppendTransportError(transportErrors, peer, "timed out waiting for search reply");
  }
  state.MailboxMutex.unlock();

  EndDistributedRequest(state, mailboxIndex);

  const size_t tookMs = static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - startedAt)
                                                .count());

  return search::MergeResponses(query, normalizedPage, normalizedLimit, sources, transportErrors,
                                tookMs);
}

// ──────────────────────────────────────────────────────────────────────────────
// HTTP layer
// ──────────────────────────────────────────────────────────────────────────────

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
  body += string(",\"parse_ms\":");
  search::AppendUnsigned(body, response.ParseMs);
  body += string(",\"solve_ms\":");
  search::AppendUnsigned(body, response.SolveMs);
  body += string(",\"collect_ms\":");
  search::AppendUnsigned(body, response.CollectMs);
  body += string(",\"rank_ms\":");
  search::AppendUnsigned(body, response.RankMs);
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

void SendResponse(int clientFd, int statusCode, const char* statusText, const char* contentType,
                  const string& body)
{
  string headers("HTTP/1.1 ");
  search::AppendUnsigned(headers, static_cast<size_t>(statusCode));
  headers += string(" ");
  headers += string(statusText);
  headers += string("\r\nContent-Type: ");
  headers += string(contentType);
  headers += string("\r\nAccess-Control-Allow-Origin: *");
  headers += string("\r\nAccess-Control-Allow-Methods: GET, OPTIONS");
  headers += string("\r\nAccess-Control-Allow-Headers: Accept, Content-Type");
  headers += string("\r\nContent-Length: ");
  search::AppendUnsigned(headers, body.size());
  headers += string("\r\nConnection: close\r\n\r\n");
  headers += body;
  send(clientFd, headers.cstr(), headers.size(), 0);
}

void SendJsonResponse(int clientFd, int statusCode, const char* statusText, const string& body)
{
  SendResponse(clientFd, statusCode, statusText, "application/json", body);
}

void HandleClient(int clientFd, SearchServerState& state)
{
  // Read the full HTTP request headers, looping until we see the header terminator
  // (\r\n\r\n) or exceed the buffer limit.
  static const size_t kRecvBufSize = 16384;
  char buffer[kRecvBufSize];
  size_t totalBytes = 0;

  while (totalBytes < kRecvBufSize - 1)
  {
    const ssize_t n = recv(clientFd, buffer + totalBytes, kRecvBufSize - 1 - totalBytes, 0);
    if (n <= 0)
      break;
    totalBytes += static_cast<size_t>(n);

    // Search only the newly-arrived tail to avoid rescanning old bytes.
    const size_t scanFrom = totalBytes >= 4 ? totalBytes - 4 : 0;
    bool complete = false;
    for (size_t i = scanFrom; i + 3 < totalBytes; ++i)
    {
      if (buffer[i] == '\r' && buffer[i + 1] == '\n' && buffer[i + 2] == '\r' &&
          buffer[i + 3] == '\n')
      {
        complete = true;
        break;
      }
    }
    if (complete)
      break;
  }

  if (totalBytes == 0)
    return;

  buffer[totalBytes] = '\0';
  const string request(buffer);
  const size_t lineEnd = request.find("\r\n");
  const size_t methodEnd = FindChar(request, ' ');
  const size_t targetEnd = FindChar(request, ' ', methodEnd == string::npos ? 0 : methodEnd + 1);
  const bool isGet = StartsWith(request, "GET ");
  const bool isOptions = StartsWith(request, "OPTIONS ");

  if (methodEnd == string::npos || targetEnd == string::npos || methodEnd >= lineEnd ||
      targetEnd > lineEnd || (!isGet && !isOptions))
  {
    SendJsonResponse(
        clientFd, 405, "Method Not Allowed",
        "{\"query\":\"\",\"page\":1,\"errors\":[{\"message\":\"GET or OPTIONS only\"}],"
        "\"results\":[]}");
    return;
  }

  const string target = CopyRange(request, methodEnd + 1, targetEnd);
  if (isOptions)
  {
    SendResponse(clientFd, 204, "No Content", "text/plain; charset=utf-8", string());
    return;
  }

  if (!StartsWith(target, "/api/search"))
  {
    const string staticPath = ResolveStaticPath(target);
    if (staticPath.size() == 0)
    {
      SendJsonResponse(clientFd, 404, "Not Found",
                       "{\"query\":\"\",\"page\":1,\"errors\":[{\"message\":\"not found\"}],"
                       "\"results\":[]}");
      return;
    }

    string body;
    if (!ReadFile(staticPath, body))
    {
      SendJsonResponse(clientFd, 404, "Not Found",
                       "{\"query\":\"\",\"page\":1,\"errors\":[{\"message\":\"not found\"}],"
                       "\"results\":[]}");
      return;
    }

    SendResponse(clientFd, 200, "OK", GuessContentType(staticPath), body);
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
      else if (key == string("page") && value.size() != 0)
        page = static_cast<size_t>(strtoul(value.cstr(), nullptr, 10));
      else if (key == string("limit") && value.size() != 0)
        limit = search::ClampSearchLimit(static_cast<size_t>(strtoul(value.cstr(), nullptr, 10)));

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
    SharedLockGuard<SharedMutex> guard(state.ServiceMutex);
    response = state.Service.SearchPage(query, page, limit);
  }

  string body;
  AppendResultString(body, response);
  SendJsonResponse(clientFd, 200, "OK", body);
}

// Thread entry point for per-client threads. Owns the socket fd.
struct ClientArgs
{
  int fd;
  SearchServerState* state;
};

static void* clientThread(void* arg)
{
  ClientArgs* args = static_cast<ClientArgs*>(arg);
  HandleClient(args->fd, *args->state);
  close(args->fd);
  delete args;
  return nullptr;
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
    runNode(static_cast<uint64_t>(localNodeId), numPeers);

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
      listen(serverFd, 64) != 0)
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

    ClientArgs* args = new ClientArgs{clientFd, &state};
    pthread_t thread;
    if (pthread_create(&thread, nullptr, clientThread, args) != 0)
    {
      close(clientFd);
      delete args;
      continue;
    }
    pthread_detach(thread);
  }
}
