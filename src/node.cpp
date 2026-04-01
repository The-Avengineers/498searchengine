#include <arpa/inet.h>
#include <avengine/avengine_lib/lock_guard.h>
#include <avengine/avengine_lib/shared_mutex.h>
#include <avengine/avengine_lib/thread.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/codec.h>
#include <avengine/crawler/fetcher.h>
#include <avengine/crawler/frontier_message.h>
#include <avengine/crawler/inbound_url_queue.h>
#include <avengine/crawler/logger.h>
#include <avengine/node.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

constexpr const uint16_t BROADCAST_PORT = 46856;
constexpr const uint16_t TCP_PORT = BROADCAST_PORT + 1;
constexpr const uint32_t BROADCAST_INTERVAL = 8;
constexpr const int SEND_TIMEOUT_SEC = 5;      // seconds before sendMessage gives up
constexpr const int CONNECT_TIMEOUT_MS = 8000; // ms before updatePeer gives up on connect

constexpr size_t HEADER_SIZE = 24;
constexpr uint64_t MAGIC = 0xF09F9B9C;
// Upper bound on the number of simultaneous peers we track.
constexpr size_t MAX_PEERS = 256;
// Fix 3: reject payloads larger than ~4 GB to prevent unbounded allocation.
constexpr uint64_t MAX_PAYLOAD_LEN = static_cast<uint64_t>(UINT32_MAX);

// Forward declaration, defined after handleMessage().
static void spawnPeerRecvThread(size_t peerIdx, int fd);

// Cluster configuration, read from environment variables at startup.
size_t avengine::localNodeId = 0;
size_t avengine::numPeers = 1;

struct Peer
{
  sockaddr_in addr; // IPv4 address + port of this peer
  time_t lastResponse = time(nullptr);
  // Nodes with a lower ID always initiate the outgoing TCP connection to nodes with a higher ID.
  // This prevents duplicate connections: each pair (lo, hi) has exactly one socket, owned by lo.
  // Higher-ID nodes only ever accept incoming connections; their tcpFd is set by tcpServer.
  int tcpFd = -1; // TCP socket for this peer; -1 means not yet connected
};

static struct
{
  avengine::vector<Peer> peers;
  bool consensusReached = false;
  size_t expectedPeers = 0;
  avengine::SharedMutex mu; // protects all fields in this struct

  void updatePeer(size_t id, sockaddr_in addr)
  {
    // Phase 1 (write lock): update address / timestamp; decide whether a new
    // TCP connection needs to be established; capture the addr we'll connect to
    // so the lock can be dropped before the blocking `connect()`.
    int oldFd = -1;
    bool needConnect = false;
    in_addr_t connectAddr = addr.sin_addr.s_addr;

    {
      avengine::lock_guard<avengine::SharedMutex> guard(mu);

      if (id >= peers.size())
      {
        // TODO: Log error
        return;
      }

      // If the peer's IP changed, signal the old connection to tear down.
      // Don't reset tcpFd here – peerRecvThread owns the fd and will call
      // close() and reset tcpFd itself once shutdown() wakes it from recv().
      if (peers[id].addr.sin_addr.s_addr != addr.sin_addr.s_addr && peers[id].tcpFd >= 0)
      {
        oldFd = peers[id].tcpFd;
      }

      peers[id].addr.sin_addr.s_addr = addr.sin_addr.s_addr;
      peers[id].lastResponse = time(nullptr);

      if (peers[id].tcpFd < 0 && avengine::localNodeId < id)
      {
        needConnect = true;
      }
    }

    // Shut down the stale connection outside the lock. shutdown() wakes the
    // old peerRecvThread from recv() without releasing the fd number, so the
    // OS cannot recycle it before peerRecvThread calls close().
    if (oldFd >= 0)
    {
      shutdown(oldFd, SHUT_RDWR);
    }

    if (!needConnect)
    {
      return;
    }

    // Phase 2 (no lock): perform the TCP connect without holding the mutex so
    // other threads are never stalled by a slow or unreachable peer.
    // connectWithTimeout() uses non-blocking connect + poll(), so it will
    // return within `CONNECT_TIMEOUT_MS` regardless of network conditions.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
      perror("socket (tcp connect)");
      return;
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = connectAddr;
    target.sin_port = htons(TCP_PORT);

    int socketError = 0;
    bool connected = false;
    if (avengine::crawler::connectWithTimeout(fd, reinterpret_cast<sockaddr*>(&target),
                                              sizeof(target), CONNECT_TIMEOUT_MS, &socketError))
    {
      // Phase 3 (write lock): store the fd only if the peer record hasn't
      // changed while we were connecting (another thread may have updated the
      // IP or already stored a socket for this peer).
      avengine::lock_guard<avengine::SharedMutex> guard(mu);
      if (id < peers.size() && peers[id].addr.sin_addr.s_addr == connectAddr && peers[id].tcpFd < 0)
      {
        peers[id].tcpFd = fd;
        connected = true;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &target.sin_addr, ip, sizeof(ip));
        printf("[tcp] connected to peer #%zu (%s)\n", id, ip);
      }
      else
      {
        // Peer state changed while we were connecting; discard this socket.
        close(fd);
      }
    }
    else
    {
      fprintf(stderr, "[tcp] connect to peer #%zu failed: %s\n", id, strerror(socketError));
      close(fd);
    }

    if (connected)
    {
      spawnPeerRecvThread(id, fd);
    }
  }
} peerList;

// Per-message-type handlers
// Each function receives the index of the peer that sent the message and the
// raw payload bytes extracted from the framed TCP message.
static void handleFrontierUpdate(size_t peerIndex, const avengine::vector<uint8_t>& payload)
{
  avengine::vector<avengine::crawler::FullURLEntry> entries;
  if (!avengine::crawler::decodeFrontierBatch(payload, entries))
  {
    fprintf(stderr, "[frontier_update] failed to decode payload from peer #%zu\n", peerIndex);
    return;
  }

  printf("[frontier_update] received %zu URL(s) from peer #%zu\n", entries.size(), peerIndex);

  for (size_t i = 0; i < entries.size(); ++i)
  {
    avengine::crawler::pushInbound(entries[i]);
  }
}

// Dispatch an incoming message to the correct handler based on its type.
static void handleMessage(size_t peerIndex, uint64_t messageType,
                          const avengine::vector<uint8_t>& payload)
{
  switch (static_cast<avengine::MessageType>(messageType))
  {
  case avengine::MessageType::FRONTIER_UPDATE:
    handleFrontierUpdate(peerIndex, payload);
    break;
  default:
    printf("[tcp recv] unknown message type %llu from peer #%zu (%zu payload bytes)\n",
           static_cast<unsigned long long>(messageType), peerIndex, payload.size());
    break;
  }
}

static void* udpGossip(void* arg);
static void* udpBroadcast(void* arg);
static void* tcpServer(void* arg);

void avengine::runNode(uint64_t nodeId, size_t numPeers)
{
  localNodeId = nodeId; // publish to file scope before any threads start

  // Pre-size peerList.peers so that updatePeer() can index into it.
  // This must happen before any threads start so no lock is required here.
  peerList.peers = avengine::vector<Peer>(numPeers);
  peerList.expectedPeers = numPeers;

  avengine::thread gossipThread(udpGossip, nullptr);
  avengine::thread broadcastThread(udpBroadcast, nullptr);
  avengine::thread tcpServerThread(tcpServer, nullptr);
}

static void* tcpServer(void* /*arg*/)
{
  int listenSock = socket(AF_INET, SOCK_STREAM, 0);
  if (listenSock < 0)
  {
    perror("socket (tcpServer)");
    return nullptr;
  }

  int reuse = 1;
  if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    perror("setsockopt SO_REUSEADDR (tcpServer)");
  }

  sockaddr_in serverAddr{};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(TCP_PORT);
  serverAddr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listenSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
  {
    perror("bind (tcpServer)");
    close(listenSock);
    return nullptr;
  }

  if (listen(listenSock, 16) < 0)
  {
    perror("listen");
    close(listenSock);
    return nullptr;
  }

  printf("[tcp] listening on TCP port %u\n", TCP_PORT);

  while (true)
  {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(listenSock, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientFd < 0)
    {
      perror("accept");
      continue;
    }

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

    // Match the incoming connection to a known peer by its source IP address.
    // Write lock: atomically install clientFd and capture any old fd.
    size_t matchedIdx = peerList.peers.size(); // sentinel: no match
    int oldFd = -1;
    {
      avengine::lock_guard<avengine::SharedMutex> guard(peerList.mu);
      for (size_t i = 0; i < peerList.peers.size(); i++)
      {
        if (peerList.peers[i].addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr)
        {
          oldFd = peerList.peers[i].tcpFd; // capture before overwriting
          peerList.peers[i].tcpFd = clientFd;
          printf("[tcp] accepted connection from peer #%zu (%s)\n", i, clientIP);
          matchedIdx = i;
          break;
        }
      }
    }

    if (matchedIdx < peerList.peers.size())
    {
      spawnPeerRecvThread(matchedIdx, clientFd);
      // Wake the old peerRecvThread (if any) after the new one is spawned so
      // clientFd is guaranteed to already be installed in peers[matchedIdx].
      // shutdown() does not release the fd number, preventing fd recycling.
      if (oldFd >= 0)
        shutdown(oldFd, SHUT_RDWR);
    }
    else
    {
      printf("[tcp] rejected connection from unknown host %s\n", clientIP);
      close(clientFd);
    }
  }

  close(listenSock);
  return nullptr;
}

// Argument passed to each per-peer receive thread.
struct PeerRecvArg
{
  size_t peerIdx;
  int fd;
};

static bool recvBytes(int fd, uint8_t* buf, size_t size)
{
  size_t read = 0;
  while (read <= size)
  {
    ssize_t n = recv(fd, buf + read, size - read, 0);
    if (n < 0 && errno == EINTR)
    {
      continue;
    }

    if (n <= 0)
    {
      if (n == 0)
      {
        printf("[tcp recv] peer disconnected\n");
      }
      else
      {
        perror("recv header (recvMessage)");
      }

      return false;
    }
    read += static_cast<size_t>(n);
  }

  return true;
}

// recvMessage reads one complete framed message from fd.
//
// Wire layout (all fields 64-bit little-endian):
//   bytes  0– 7 : magic       (0xF09F9B9C)
//   bytes  8–15 : messageType
//   bytes 16–23 : payloadLen
//   bytes 24+   : payload     (payloadLen bytes)
//
// On success, populates messageType and payload and returns true.
// On disconnect or protocol error, logs and returns false.
// peerIdx is used only for log messages.
static bool recvMessage(int fd, size_t peerIdx, uint64_t& messageType,
                        avengine::vector<uint8_t>& payload)
{
  uint8_t header[HEADER_SIZE];
  recvBytes(fd, header, HEADER_SIZE);

  avengine::Reader r(header, HEADER_SIZE);
  uint64_t magic = r.readU64();
  messageType = r.readU64();
  uint64_t payloadLen = r.readU64();

  if (magic != MAGIC)
  {
    fprintf(stderr, "[tcp recv] bad magic 0x%llx from peer #%zu – disconnecting\n",
            static_cast<unsigned long long>(magic), peerIdx);
    return false;
  }

  // Guard against a malicious or corrupted payloadLen that would cause an
  // unbounded allocation and crash the process.
  if (payloadLen > MAX_PAYLOAD_LEN)
  {
    fprintf(stderr,
            "[tcp recv] peer #%zu sent oversized payloadLen %llu (max %llu) – disconnecting\n",
            peerIdx, static_cast<unsigned long long>(payloadLen),
            static_cast<unsigned long long>(MAX_PAYLOAD_LEN));
    return false;
  }

  payload = avengine::vector<uint8_t>(static_cast<size_t>(payloadLen));
  recvBytes(fd, payload.begin(), payloadLen);

  return true;
}

// peerRecvThread is the entry point for each per-peer receive thread.
//
// It owns `fd` for the lifetime of the connection: it calls recvMessage() in a
// loop, dispatching each complete message to handleMessage(), and on disconnect
// or protocol error closes `fd`, resets peers[peerIdx].tcpFd to -1, and exits.
// Because each peer has its own thread there is no head-of-line blocking: a
// stalled peer only blocks its own thread.
static void* peerRecvThread(void* arg)
{
  PeerRecvArg a = *static_cast<PeerRecvArg*>(arg);
  delete static_cast<PeerRecvArg*>(arg);
  const size_t peerIdx = a.peerIdx;
  const int fd = a.fd;

  uint64_t messageType;
  avengine::vector<uint8_t> payload;

  while (recvMessage(fd, peerIdx, messageType, payload))
  {
    handleMessage(peerIdx, messageType, payload);
  }

  // peerRecvThread is the sole owner of fd. Only reset tcpFd if it still holds
  // our fd (a reconnect may have already installed a new socket, in which case
  // we leave it untouched).
  close(fd);
  avengine::lock_guard<avengine::SharedMutex> guard(peerList.mu);
  if (peerIdx < peerList.peers.size() && peerList.peers[peerIdx].tcpFd == fd)
  {
    peerList.peers[peerIdx].tcpFd = -1;
  }
  return nullptr;
}

// Spawn a detached peerRecvThread for the connection at (peerIdx, fd).
// On thread start failure, closes fd and resets peers[peerIdx].tcpFd.
static void spawnPeerRecvThread(size_t peerIdx, int fd)
{
  auto* arg = new PeerRecvArg{peerIdx, fd};
  avengine::thread t(peerRecvThread, arg);
  if (t.detach())
  {
    return; // thread is running and detached; it owns arg and fd from here on
  }

  // Thread start failed so no thread was started; we own fd and must clean
  // up the same way peerRecvThread would have.
  fprintf(stderr, "[tcp recv] failed to spawn recv thread for peer #%zu\n", peerIdx);
  delete arg;
  close(fd);
  avengine::lock_guard<avengine::SharedMutex> guard(peerList.mu);
  if (peerIdx < peerList.peers.size() && peerList.peers[peerIdx].tcpFd == fd)
  {
    peerList.peers[peerIdx].tcpFd = -1;
  }
}

static void* udpGossip(void* /*arg*/)
{
  // Open UDP server on port `BROADCAST_PORT`.
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
  {
    perror("socket");
    return nullptr;
  }

  // Allow rapid restart without waiting for TIME_WAIT to expire.
  int reuse = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    perror("setsockopt SO_REUSEADDR");
  }

  // Bind to INADDR_ANY so we receive datagrams on all interfaces.
  sockaddr_in serverAddr{};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(BROADCAST_PORT);
  serverAddr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
  {
    perror("bind");
    close(sock);
    return nullptr;
  }

  printf("[gossip] listening on UDP port %u\n", BROADCAST_PORT);

  // Gossip receive loop: block on recvfrom and handle each incoming datagram.
  uint8_t buf[4096];
  sockaddr_in senderAddr{};
  socklen_t senderLen = sizeof(senderAddr);

  while (true)
  {
    ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&senderAddr),
                         &senderLen);
    if (n < 0)
    {
      perror("recvfrom");
      continue;
    }

    if (n < 16)
    {
      continue;
    }

    // Packet is not associated with our protocol
    avengine::Reader pktReader(buf, static_cast<size_t>(n));
    auto pktMagic = pktReader.readU64();
    auto pktSenderID = pktReader.readU64();
    if (pktMagic != MAGIC)
    {
      // TODO: Log maybe?
      continue;
    }

    char senderIP[INET_ADDRSTRLEN];
    size_t senderID = static_cast<size_t>(pktSenderID);
    inet_ntop(AF_INET, &senderAddr.sin_addr, senderIP, sizeof(senderIP));
    printf("[gossip] received %zd bytes from %s:%u #%zu\n", n, senderIP, ntohs(senderAddr.sin_port),
           senderID);

    peerList.updatePeer(senderID, senderAddr);
  }

  close(sock);
  return nullptr;
}

static bool sendBytes(int fd, avengine::vector<uint8_t>& buf)
{
  size_t sent = 0;
  while (sent < buf.size())
  {
    ssize_t n = send(fd, buf.begin() + sent, buf.size() - sent, 0);
    if (n < 0 && errno == EINTR)
    {
      continue;
    }
    if (n <= 0)
    {
      perror("sending bytes to peer");
      return true;
    }
    sent += static_cast<size_t>(n);
  }
  return false;
}

// sendMessage sends a length-prefixed, typed message to the peer identified by
// ID over its established TCP connection.
//
// Wire layout (all fields 64-bit little-endian):
//   bytes  0– 7 : magic       (0xF09F9B9C – same sentinel used by UDP gossip)
//   bytes  8–15 : messageType
//   bytes 16–23 : payloadLen  (number of payload bytes that follow)
//   bytes 24+   : payload     (message.size() bytes)
//
// Returns true on error, false on success.
// Sends exactly `buf.size()` bytes over `fd`, retrying transparently on
// signal interruption (EINTR).  Returns true on error, false on success.
bool avengine::sendMessage(size_t ID, avengine::MessageType messageType,
                           avengine::vector<uint8_t>& message)
{
  avengine::SharedLockGuard<avengine::SharedMutex> guard(peerList.mu);

  if (ID >= peerList.peers.size())
  {
    fprintf(stderr, "[sendMessage] peer ID %zu is out of range (have %zu peers)\n", ID,
            peerList.peers.size());
    return true;
  }

  int fd = peerList.peers[ID].tcpFd;

  if (fd < 0)
  {
    fprintf(stderr, "[sendMessage] peer #%zu has no active TCP connection\n", ID);
    return true;
  }

  timeval tv{};
  tv.tv_sec = SEND_TIMEOUT_SEC;
  tv.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const void*>(&tv), sizeof(tv)) < 0)
  {
    perror("setsockopt SO_SNDTIMEO (sendMessage)");
  }

  avengine::Writer headerWriter;
  headerWriter.writeU64(MAGIC);
  headerWriter.writeU64(static_cast<uint64_t>(messageType));
  headerWriter.writeU64(static_cast<uint64_t>(message.size()));

  avengine::vector<uint8_t> headerBytes = headerWriter.release();

  return sendBytes(fd, headerBytes) || sendBytes(fd, message);
}

static void* udpBroadcast(void* /*arg*/)
{
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
  {
    perror("socket");
    return nullptr;
  }

  // Enable sending to the subnet broadcast address.
  int broadcast = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
  {
    perror("setsockopt SO_BROADCAST");
    close(sock);
    return nullptr;
  }

  // Target the limited broadcast address so every host on the local subnet
  // receives the datagram.
  sockaddr_in destAddr{};
  destAddr.sin_family = AF_INET;
  destAddr.sin_port = htons(BROADCAST_PORT);
  destAddr.sin_addr.s_addr = INADDR_BROADCAST; // 255.255.255.255

  // Read the node ID from the file-scope localNodeId and embed it in the
  // broadcast packet.
  // Packet layout (16 bytes, all little-endian):
  //   bytes 0–7 : magic number 0xF09F9B9C
  //   bytes 8–15: this node's ID
  avengine::Writer pktWriter;
  pktWriter.writeU64(MAGIC);
  pktWriter.writeU64(avengine::localNodeId);
  const socklen_t destLen = sizeof(destAddr);

  while (true)
  {
    ssize_t n = sendto(sock, pktWriter.data(), pktWriter.size(), 0,
                       reinterpret_cast<const sockaddr*>(&destAddr), destLen);
    if (n < 0)
    {
      perror("sendto");
    }
    else
    {
      printf("[broadcast] sent %zd bytes to 255.255.255.255:%u\n", n, BROADCAST_PORT);
    }

    sleep(BROADCAST_INTERVAL);
  }

  close(sock);
  return nullptr;
}
