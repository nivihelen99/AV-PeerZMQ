# AV-PeerZMQ Requirements

This document outlines the functional and non-functional requirements for the AV-PeerZMQ library.

## 1. Introduction

AV-PeerZMQ is a C++ library designed to facilitate peer-to-peer (P2P) mesh networking. It provides functionalities for dynamic peer discovery, message passing, and robust network management.

## 2. Functional Requirements

### FR1: Peer Discovery
- **FR1.1: Seed Node Discovery:** The library must allow applications to specify a list of initial "seed nodes." The library will attempt to connect to these seed nodes upon startup to join the mesh network.
- **FR1.2: UDP Broadcast Discovery:** The library must support discovery of peers on the local area network (LAN) using UDP broadcasts. Nodes should be able to announce their presence and listen for announcements from other nodes.
- **FR1.3: Peer List Exchange:** Upon discovering a new peer (either through seed nodes or UDP broadcast), nodes should exchange their lists of known peers to facilitate faster network graph convergence.

### FR2: Messaging
- **FR2.1: Unicast Messaging:** The library must allow applications to send a message to a specific, known peer in the network. Delivery should be reliable (e.g., using TCP).
- **FR2.2: Broadcast Messaging:** The library must allow applications to send a message to all directly connected peers. This is a one-to-many message propagation to immediate neighbors.
- **FR2.3: Message Serialization:** Messages should be serialized in a structured format. JSON is used for this purpose.

### FR3: Peer Management
- **FR3.1: Heartbeating:** The library must implement a heartbeat mechanism to monitor the health and reachability of connected peers.
- **FR3.2: Timeout Detection:** If a peer does not send a heartbeat or respond within a configurable timeout period, it should be considered disconnected.
- **FR3.3: Dynamic Connection Management:** The library must dynamically manage connections to peers, establishing new connections as peers are discovered and tearing down connections when peers become unresponsive or explicitly leave the network.
- **FR3.4: Graceful Shutdown (Goodbye Message):** Nodes should be able to announce their departure from the network to their connected peers, allowing for a cleaner disconnection.

### FR4: Application Callbacks
- **FR4.1: Unicast Message Callback:** The library must provide a mechanism for applications to register a callback function that is invoked when a unicast message is received. The callback should provide the sender's identity and the message content.
- **FR4.2: Broadcast Message Callback:** The library must provide a mechanism for applications to register a callback function that is invoked when a broadcast message is received. The callback should provide the sender's identity and the message content.

## 3. Non-Functional Requirements

### NFR1: Resilience
- **NFR1.1: Dynamic Topology:** The library should be resilient to nodes joining and leaving the network dynamically.
- **NFR1.2: Connection Reattempts:** The library may attempt to reconnect to peers if connections fail, though this is not a strict requirement for the current version beyond initial seed node connection attempts.

### NFR2: Portability
- **NFR2.1: Cross-Platform Compatibility:** The library should be designed to be compilable and runnable on major operating systems (e.g., Linux, macOS, Windows) with minimal platform-specific code. This is achieved by using standard C++ and cross-platform dependencies like ZeroMQ.

### NFR3: Ease of Integration
- **NFR3.1: Simple API:** The library should expose a clear and easy-to-use API for application developers.
- **NFR3.2: Build System:** CMake should be used for building the library, simplifying the integration process for dependent projects.

### NFR4: Performance
- **NFR4.1: Responsiveness:** The library should handle network events and messages in a timely manner.
- **NFR4.2: Scalability Considerations:** While the current full-mesh TCP model has inherent scalability limits, the design should be mindful of potential future optimizations. For very large networks (hundreds of nodes), the current broadcast mechanism (sending to each peer individually) might become a bottleneck. This is a known limitation.

### NFR5: Resource Usage
- **NFR5.1: Threading:** The library will use multiple threads for tasks such as message handling, heartbeating, and discovery to ensure non-blocking operations. Resource management for these threads should be handled efficiently.
- **NFR5.2: Dependency Management:** External dependencies (like ZeroMQ and nlohmann/json) should be managed clearly, with nlohmann/json fetched via CMake.

## 4. Message Protocol (Reference)

- All messages adhere to a common JSON structure:
  ```json
  {
    "type": <MessageType_enum_value>,
    "data": {
      "sender": "<ip:port_string>",
      "timestamp": <milliseconds_since_epoch>,
      // ... other type-specific fields
    }
  }
  ```
- `MessageType` includes `HEARTBEAT`, `PEER_DISCOVERY`, `UNICAST`, `BROADCAST`, `PEER_LIST`, `GOODBYE`.
