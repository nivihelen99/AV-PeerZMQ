# AV-PeerZMQ Design Document

This document provides a detailed design of the AV-PeerZMQ library, covering its architecture, core components, communication protocols, and key mechanisms.

## 1. Introduction

AV-PeerZMQ is a C++ library aimed at simplifying the development of decentralized peer-to-peer (P2P) applications. It provides a mesh networking layer that handles dynamic peer discovery, reliable messaging, and network resilience. This document details the internal design of the library.

## 2. High-Level Architecture

AV-PeerZMQ operates as a full-mesh network where each node aims to connect directly to all other known nodes. The library is structured around a central `MeshNetwork` class that manages all aspects of network interaction.

The architecture involves:
- **Peer Discovery:** Mechanisms for nodes to find each other, both initially (via seed nodes) and dynamically (via UDP broadcasts on the LAN).
- **Connection Management:** Establishing and maintaining TCP connections between peers for reliable message exchange.
- **Message Passing:** Supporting unicast (one-to-one) and broadcast (one-to-all-direct-neighbors) messaging.
- **Concurrency:** Utilizing multiple threads to handle network I/O, background tasks (like heartbeats and discovery), and application callbacks without blocking the main application thread.

## 3. Core Components

### 3.1. `MeshNetwork` Class
This is the primary public interface of the library. An application instantiates and interacts with this class to join the network, send/receive messages, and manage its network presence.

**Key Responsibilities:**
- Managing the local node's identity (`local_node_`).
- Starting and stopping network operations.
- Handling peer connections and disconnections.
- Dispatching incoming messages to appropriate handlers or application callbacks.
- Coordinating background threads for discovery, heartbeating, and cleanup.

### 3.2. `NodeId` Structure
Represents a unique identifier for a node in the network.
```cpp
struct NodeId {
    std::string ip;       // IP address of the node
    uint16_t    port;     // Main communication port of the node
    std::string id;       // Unique string identifier, typically "ip:port"
};
```
It includes comparison operators for use in STL containers. A `NodeIdHash` struct is also provided for `std::unordered_map`.

### 3.3. `PeerInfo` Structure
Stores runtime information about each known peer.
```cpp
struct PeerInfo {
    NodeId node_id;                               // Identifier of the peer
    std::chrono::steady_clock::time_point last_seen; // Timestamp of the last received message/heartbeat
    bool is_connected;                            // Flag indicating if a TCP connection is active
    std::unique_ptr<zmq::socket_t> socket;        // ZMQ_DEALER socket for communication with this peer
};
```
`PeerInfo` objects are stored in the `MeshNetwork::peers_` map.

## 4. ZeroMQ Socket Usage

ZeroMQ (libzmq) is used for underlying message transport.

### 4.1. `router_socket_` (ZMQ_ROUTER)
- **Type:** `zmq::socket_type::router` (aliased as `ZMQ_ROUTER`)
- **Binding:** Binds to the local node's main IP address and port (e.g., `tcp://*:local_port`).
- **Purpose:** This is the main listening socket for incoming TCP connections from other peers. It handles reliable, ordered communication for:
    - Unicast messages
    - Broadcast messages (received part)
    - Heartbeats
    - Peer list exchanges
    - Goodbye messages
- **Behavior:** When a peer connects, its ZMQ_DEALER socket sends messages that are received by this ZMQ_ROUTER socket. The ROUTER socket prepends a message part containing the sender's identity, allowing `MeshNetwork` to know who sent the message.

### 4.2. Peer Sockets (ZMQ_DEALER)
- **Type:** `zmq::socket_type::dealer` (aliased as `ZMQ_DEALER`)
- **Connection:** Each `PeerInfo` object, when connected, holds a `std::unique_ptr<zmq::socket_t>` which is a ZMQ_DEALER socket. This socket connects to the ZMQ_ROUTER socket of the remote peer (e.g., `tcp://peer_ip:peer_port`).
- **Purpose:** Used for sending messages (unicast, broadcast, heartbeat, etc.) to a specific remote peer.
- **Behavior:** The ZMQ_DEALER socket sends messages directly to the connected peer's ROUTER socket. It also automatically handles fair-queuing of outgoing messages.

### 4.3. `discovery_socket_` (ZMQ_SUB for EPGM)
- **Type:** `zmq::socket_type::sub` (aliased as `ZMQ_SUB`)
- **Connection:** Connects to a multicast address using the EPGM transport (e.g., `epgm://eth0;239.255.0.1:discovery_port`).
- **Subscription:** Subscribes to all messages (`""`).
- **Purpose:** Listens for peer discovery announcements broadcasted by other nodes on the LAN using EPGM (Pragmatic General Multicast), which is a reliable multicast protocol built on top of UDP.

### 4.4. Discovery Broadcasting (ZMQ_PUB for EPGM)
- **Type:** `zmq::socket_type::pub` (aliased as `ZMQ_PUB`)
- **Binding:** Binds to the same EPGM multicast address that `discovery_socket_` subscribes to (e.g., `epgm://eth0;239.255.0.1:discovery_port`).
- **Purpose:** Periodically broadcasts `PEER_DISCOVERY` messages over EPGM to announce the node's presence on the LAN. This socket is created temporarily within the `broadcast_discovery` method.

## 5. Threading Model

The `MeshNetwork` class utilizes several background threads to manage network operations asynchronously:

### 5.1. `message_handler_thread_`
- **Responsibility:** Polls the `router_socket_` (for TCP messages) and the `discovery_socket_` (for EPGM/UDP discovery messages) for incoming data.
- **Mechanism:** Uses `zmq::poll()` to wait for events on these sockets. When a message arrives:
    - For `router_socket_`: Calls `handle_message()` to parse and process the message.
    - For `discovery_socket_`: Calls `handle_discovery_message()` to process discovery announcements.
- **Importance:** This is the core I/O thread for message reception.

### 5.2. `heartbeat_thread_`
- **Responsibility:** Periodically sends `HEARTBEAT` messages to all currently connected peers.
- **Mechanism:** Iterates through the `peers_` map. For each connected peer, it constructs and sends a `HEARTBEAT` message via the peer's ZMQ_DEALER socket.
- **Frequency:** Controlled by `HEARTBEAT_INTERVAL_MS`. Includes jitter to prevent synchronized heartbeats across the network.

### 5.3. `discovery_thread_`
- **Responsibility:** Manages ongoing peer discovery.
- **Mechanisms:**
    1.  **Seed Node Connection:** Periodically attempts to connect to any configured `seed_nodes_` that are not yet connected.
    2.  **Discovery Broadcast:** Calls `broadcast_discovery()` to send out an EPGM/UDP broadcast announcing the node's presence.
- **Frequency:** Controlled by `DISCOVERY_INTERVAL_MS`. Includes jitter.

### 5.4. `cleanup_thread_`
- **Responsibility:** Periodically checks for timed-out peers.
- **Mechanism:** Iterates through the `peers_` map. If a peer's `last_seen` timestamp exceeds `PEER_TIMEOUT_MS`, the peer is considered unresponsive, and `disconnect_peer()` is called.
- **Frequency:** Controlled by `CLEANUP_INTERVAL_MS`. Includes jitter.

## 6. Internal Messaging Protocol

All messages exchanged between `MeshNetwork` instances are JSON objects.

### 6.1. Common Message Structure
```json
{
  "type": <integer>,  // Corresponds to MessageType enum
  "data": {
    "sender": "<ip:port_string>", // NodeId.id of the original sender
    "timestamp": <integer_ms_epoch>, // Message creation time
    // ... other fields specific to the message type
  }
}
```

### 6.2. `MessageType` Enum
Defines the purpose of the message.
```cpp
enum class MessageType : uint8_t {
    HEARTBEAT      = 1, // Sent periodically to keep connections alive
    PEER_DISCOVERY = 2, // Used for UDP broadcast and requesting peer lists
    UNICAST        = 3, // Application-level message for a single peer
    BROADCAST      = 4, // Application-level message for all direct peers
    PEER_LIST      = 5, // Response to PEER_DISCOVERY, contains list of known peers
    GOODBYE        = 6  // Sent when a node is gracefully shutting down
};
```
*(Note: The enum values in the actual code might differ, the README had 0-5, header has 1-6. The header is likely more current.)*

### 6.3. Key Message Payloads (`data` field content)

- **`HEARTBEAT`**:
  ```json
  { "sender": "ip:port", "timestamp": 123... }
  ```
- **`PEER_DISCOVERY` (Broadcast via EPGM/UDP)**:
  ```json
  { "sender": "ip:port", "port": <main_comm_port>, "timestamp": 123... }
  ```
  The `"port"` field here refers to the main communication port of the sender, which the receiver will use to establish a TCP connection.
- **`PEER_DISCOVERY` (TCP Request for Peer List - if used, though typically handled by direct connection and Peer List exchange)**:
  This specific request form is less emphasized than the UDP discovery and subsequent peer list exchange. Connection itself implies discovery.
- **`UNICAST`**:
  ```json
  { "sender": "ip:port", "target": "target_ip:port", "message": "...", "timestamp": 123... }
  ```
- **`BROADCAST`**:
  ```json
  { "sender": "ip:port", "message": "...", "timestamp": 123... }
  ```
- **`PEER_LIST`**:
  ```json
  { "sender": "ip:port", "peers": ["ip1:port1", "ip2:port2", ...], "timestamp": 123... }
  ```
- **`GOODBYE`**:
  ```json
  { "sender": "ip:port", "timestamp": 123... }
  ```

### 6.4. Message Parsing
The `MeshNetwork::parse_message()` function handles deserializing the JSON string into a `nlohmann::json` object and extracting the `MessageType` and the main `data` payload.

## 7. Peer Discovery Mechanisms

### 7.1. Seed Nodes
- The application can provide a list of known "seed nodes" (IP and port).
- `MeshNetwork` attempts to establish TCP connections to these seed nodes upon startup and periodically thereafter if not connected.
- Once connected to a seed node, peer lists are typically exchanged (implicitly or explicitly via `PEER_LIST` messages if a node sends a `PEER_DISCOVERY` message over TCP to a newly connected peer).

### 7.2. EPGM/UDP Broadcast Discovery
- **Broadcasting:** The `discovery_thread_` periodically calls `broadcast_discovery()`, which creates a temporary ZMQ_PUB socket and sends a `PEER_DISCOVERY` message (containing the node's `id` and main communication `port`) to a multicast group using EPGM.
- **Listening:** The `message_handler_thread_` listens on the `discovery_socket_` (ZMQ_SUB connected to the EPGM multicast address).
- **Processing:** When a `PEER_DISCOVERY` broadcast is received via `handle_discovery_message()`:
    1. The receiving node extracts the sender's `NodeId` (from "sender" field) and its main communication port.
    2. It initiates a TCP connection to the discovered peer using `connect_to_peer()`.
    3. Optionally, it might send its own `PEER_DISCOVERY` message or a `PEER_LIST` message back to the newly discovered peer over the established TCP connection to facilitate bidirectional discovery and graph synchronization. The current implementation in `handle_discovery_message` sends a `PEER_DISCOVERY` message back over the newly formed TCP link.

### 7.3. Peer List Exchange
- When a node receives a `PEER_DISCOVERY` message (typically over TCP from a newly connected peer, or if a peer explicitly requests it), it responds with a `PEER_LIST` message.
- This message contains a list of `NodeId.id` strings of all peers currently connected to the sender.
- The receiver of the `PEER_LIST` message iterates through the list and attempts to connect to any peers it doesn't already know, using `connect_to_peer()`. This helps in transitively discovering more nodes in the mesh.

## 8. Connection Management

### 8.1. `connect_to_peer(const NodeId& peer_id)`
- Called when a new peer needs to be connected (from discovery, seed list, or peer list exchange).
- If not already connected or attempting connection:
    1. Creates a `PeerInfo` entry if one doesn't exist.
    2. Creates a ZMQ_DEALER socket.
    3. Sets socket options (e.g., `ZMQ_ROUTING_ID` to `local_node_.id`, `ZMQ_LINGER`).
    4. Calls `zmq_connect()` to the peer's router address (`tcp://peer_ip:peer_port`).
    5. Marks `PeerInfo::is_connected = true` and updates `last_seen`.
- Handles exceptions during connection.

### 8.2. `disconnect_peer(const NodeId& peer_id)`
- Called when a peer times out, sends a `GOODBYE` message, or an error occurs.
- Closes the ZMQ_DEALER socket associated with the peer.
- Resets the `PeerInfo::socket` unique_ptr.
- Sets `PeerInfo::is_connected = false`.
- Removes the peer from the `peers_` map.

### 8.3. `update_peer_last_seen(const NodeId& peer_id)`
- Called whenever any valid message (including heartbeat) is received from a peer.
- Updates `PeerInfo::last_seen` to the current time. This prevents the peer from being timed out by the `cleanup_thread_`.

## 9. Error Handling and Logging
- The library uses `LOG_INFO` and `LOG_ERROR` macros for logging. In a production system, these would ideally be replaced with a more configurable logging library.
- ZMQ errors (`zmq::error_t`) are caught in critical operations (socket creation, connect, bind, send, recv, poll).
- JSON parsing errors (`nlohmann::json::exception`) are caught during message deserialization.
- Standard exceptions (`std::exception`) are also caught as a fallback.

## 10. Key Data Structures
- `MeshNetwork::peers_`: `std::unordered_map<NodeId, std::unique_ptr<PeerInfo>, NodeIdHash>`
  - Stores all known peers (connected or not recently, until cleaned up). The `NodeId` is the key, and `PeerInfo` holds the state.
- `MeshNetwork::seed_nodes_`: `std::vector<NodeId>`
  - Stores the initial list of seed nodes to connect to.

## 11. Future Considerations / Potential Improvements
- **Scalability:** For very large networks, a full mesh is not sustainable. Hierarchical topologies, gossip protocols for broader message dissemination, or pub/sub patterns for specific data streams could be explored.
- **Advanced Broadcast/Multicast:** EPGM is used for discovery. For application-level broadcasts in large LANs, native ZMQ PGM/EPGM sockets could be an option if reliable multicast is needed beyond direct peer connections, but this adds complexity.
- **Security:** Currently, there's no encryption or authentication. Integrating TLS for ZMQ sockets (ZAP protocol) would be essential for secure communication.
- **Message Prioritization/QoS:** No specific quality-of-service or message prioritization is implemented beyond ZeroMQ's default behaviors.
- **Configuration:** Many parameters (timeouts, intervals) are constants. A configuration mechanism would be beneficial.

This design document provides a snapshot of the AV-PeerZMQ library's architecture. It should be updated as the library evolves.
