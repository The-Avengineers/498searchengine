#pragma once
#include <avengine/avengine_lib/vector.h>
#include <cstddef>
#include <cstdint>

namespace avengine
{

// Cluster configuration, read from environment variables at startup.
extern size_t localNodeId;
extern size_t numPeers;

// Message types that can be sent between nodes.
// Add new values here; the dispatcher in node.cpp must have a matching case.
enum class MessageType : uint64_t
{
  FRONTIER_UPDATE,
  // Add more message types here.
};

// Initialises the gossip node with the given ID and expected peer count,
// then starts the UDP gossip, UDP broadcast, and TCP server threads.
// Blocks until all three threads exit (i.e. until the process is killed).
// nodeId   – this node's integer identity (0-based index among all peers).
// numPeers – total number of nodes in the cluster (pre-sizes the peer table).
void runNode(uint64_t nodeId, size_t numPeers);

// Sends a framed, typed message to the peer identified by ID over its
// established TCP connection.
// ID          – 0-based index of the destination peer.
// messageType – one of the MessageType values defined above.
// message     – raw payload bytes; the wire frame header is prepended internally.
// Returns false on success, true on error (unknown ID, not connected,
// send timeout, or partial write). Matches the existing error convention.
bool sendMessage(size_t ID, MessageType messageType, avengine::vector<uint8_t>& message);

} // namespace avengine
