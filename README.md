````markdown
# AV-PeerZMQ

Peer-to-Peer Mesh Networking Library

---

## 1. Project Overview

**AV-PeerZMQ** is a C++ library for building decentralized, peer-to-peer (P2P) mesh networks. It leverages ZeroMQ for asynchronous messaging and JsonCpp for message serialization. The library enables:

- **Dynamic Peer Discovery**
  - **Seed Nodes**: Bootstrap by connecting to a list of known seed nodes.
  - **UDP Broadcast Discovery**: Discover peers on the local network via UDP broadcasts. When a new peer is found via UDP, both nodes exchange peer lists to accelerate full-mesh formation.

- **Messaging**
  - **Unicast**: Send targeted messages to a specific peer.
  - **Broadcast**: Propagate messages to all directly connected peers.
  - **Peer Management**: Automatic heartbeating, timeout detection, and dynamic connection management.
  - **Callbacks**: Application-level callbacks for received unicast or broadcast messages.

- **Resilience**
  - Handles nodes joining and leaving dynamically.
  - Peers attempt to reconnect or discover alternative peers if connections fail.

- **Cross-Platform Potential**
  - Built with standard C++, ZeroMQ, and JsonCpp, making it portable across major operating systems.

---

## 2. Architecture & Components

### 2.1 Core Classes and Structures

#### `MeshNetwork`

The primary interface for applications. It encapsulates:

- **Peer Discovery** (via UDP and TCP).
- **Message Handling** (unicast, broadcast, heartbeats, peer lists).
- **Connection Management**.

#### `NodeId`

A struct representing a unique node:

```cpp
struct NodeId {
    std::string ip;
    uint16_t port;
    std::string id; // "ip:port"
};
````

#### `PeerInfo`

Holds runtime information for each connected peer:

```cpp
struct PeerInfo {
    NodeId               node_id;
    std::chrono::steady_clock::time_point last_seen;
    bool                 is_connected;
    std::unique_ptr<zmq::socket_t> dealer_socket; // ZMQ_DEALER for sending messages
};
```

### 2.2 ZeroMQ Sockets

* **`router_socket_` (ZMQ\_ROUTER)**

  * Binds to the node’s main IP and port over TCP.
  * Handles reliable, ordered communication: unicast, broadcast, heartbeats, and peer-list exchanges.

* **`discovery_socket_` (ZMQ\_DGRAM)**

  * Binds to a UDP port (default: `local_port + 1000`).
  * Sends and receives peer-discovery broadcasts on the LAN.

### 2.3 Internal Threads

* **`message_handler_thread_`**

  * Polls both `router_socket_` (TCP) and `discovery_socket_` (UDP).
  * Dispatches incoming messages to `handle_message()` or `handle_discovery_message()`.

* **`heartbeat_thread_`**

  * Periodically sends heartbeat messages to all connected peers.

* **`discovery_thread_`**

  * Periodically attempts to connect to configured seed nodes over TCP.
  * Broadcasts peer-discovery messages via UDP.

* **`cleanup_thread_`**

  * Periodically scans for peers that have not sent a heartbeat within the timeout period and disconnects them.

### 2.4 Message Types (Internal Protocol)

All messages are JSON objects with at least:

* `"type"`: Integer matching one of the `MessageType` enum values.
* `"data"`: Payload object containing:

  * `"sender"`: Originating node’s `NodeId.id`.
  * `"timestamp"`: Creation time.
  * Type-specific fields.

```cpp
enum class MessageType {
    HEARTBEAT     = 0,
    PEER_DISCOVERY = 1, // used in UDP broadcasts or TCP requests for peer lists
    UNICAST       = 2,  // application-level unicast
    BROADCAST     = 3,  // application-level broadcast
    PEER_LIST     = 4,  // response to TCP PEER_DISCOVERY, contains an array of peers
    GOODBYE       = 5   // graceful node shutdown notification
};
```

---

## 3. Prerequisites

Before building AV-PeerZMQ, install the following:

1. **C++ Compiler**

   * GCC (≥ 4.8) or Clang (≥ 3.3) with C++11 support.

2. **Make Utility**

   * `sudo apt-get install make` (Debian/Ubuntu)
   * `sudo dnf install make` (Fedora)
   * `brew install make` (macOS)

3. **ZeroMQ Development Libraries (≥ 4.x.x)**

   * Debian/Ubuntu: `sudo apt-get install libzmq3-dev`
   * Fedora: `sudo dnf install zeromq-devel`
   * macOS (Homebrew): `brew install zeromq`

4. **JsonCpp Development Libraries (≥ 1.x.x)**

   * Debian/Ubuntu: `sudo apt-get install libjsoncpp-dev`
   * Fedora: `sudo dnf install jsoncpp-devel`
   * macOS (Homebrew): `brew install jsoncpp`

5. **pkg-config Utility**

   * Debian/Ubuntu: `sudo apt-get install pkg-config`
   * Fedora: `sudo dnf install pkgconfig`
   * macOS (Homebrew): `brew install pkg-config`

> *If ZeroMQ or JsonCpp are installed in custom locations not managed by `pkg-config`, set the `PKG_CONFIG_PATH` environment variable or pass `INC_PATHS` and `LIB_PATHS` to `make`.*

---

## 4. Building the Project

AV-PeerZMQ includes two Makefiles:

* **`Makefile`**: Builds the example application (`example_app`).
* **`Makefile.tests`**: Builds the test suite (`test_mesh_network`).

### 4.1 Example Application (`example_app`)

1. **Build**

   ```bash
   make
   ```

   * Compiles `mesh_network.cpp` and `example_app.cpp`.
   * Links against ZeroMQ, JsonCpp, and pthread.
   * Produces `example_app` in the project root.

2. **Clean**

   ```bash
   make clean
   ```

3. **Manual Compile Command** (if not using `make`)

   ```bash
   g++ -std=c++11 -Wall -o example_app example_app.cpp mesh_network.cpp \
       -I/usr/local/include -L/usr/local/lib -lzmq -ljsoncpp -pthread
   ```

### 4.2 Test Suite (`test_mesh_network`)

1. **Build**

   ```bash
   make -f Makefile.tests build_tests
   ```

   or simply:

   ```bash
   make -f Makefile.tests
   ```

   * Compiles `mesh_network.cpp` and `test_mesh_network.cpp`.
   * Produces `test_mesh_network`.

2. **Clean**

   ```bash
   make -f Makefile.tests clean_tests
   ```

---

## 5. Usage Instructions & Examples

### 5.1 Integrating the Library

1. Include `mesh_network.h` in your C++ application.
2. Compile and link `mesh_network.cpp` with your application. The `Makefile` handles this for `example_app`.

### 5.2 `example_app` Command-Line Interface

```cpp
#include "mesh_network.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

// Global control
static std::atomic<bool> app_running(true);
static MeshNetwork* network_ptr = nullptr;

// Signal handler for graceful shutdown
void signal_handler(int signum) {
    std::cout << "\nCaught signal " << signum << ". Shutting down...\n";
    app_running = false;
    if (network_ptr) {
        network_ptr->stop();
    }
}

// Application callbacks
void unicast_handler(const NodeId& sender, const std::string& message) {
    std::cout << "[App] Unicast from " << sender.id << ": " << message << "\n";
}

void broadcast_handler(const NodeId& sender, const std::string& message) {
    bool is_self = (network_ptr && sender.id == network_ptr->get_local_node_id().id);
    std::cout << "[App] Broadcast from " << sender.id << (is_self ? " (self)" : "") 
              << ": " << message << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <local_ip> <local_port> [seed_ip1:port1 ...]\n";
        return 1;
    }

    std::string local_ip   = argv[1];
    uint16_t local_port    = static_cast<uint16_t>(std::stoi(argv[2]));
    MeshNetwork network(local_ip, local_port);
    network_ptr = &network;

    // Register callbacks
    network.set_unicast_callback(unicast_handler);
    network.set_broadcast_callback(broadcast_handler);

    // Add seed nodes (if provided)
    for (int i = 3; i < argc; ++i) {
        std::string seed_address = argv[i];
        auto colon_pos = seed_address.find(':');
        if (colon_pos != std::string::npos) {
            std::string seed_ip   = seed_address.substr(0, colon_pos);
            uint16_t seed_port    = static_cast<uint16_t>(
                                        std::stoi(seed_address.substr(colon_pos + 1))
                                     );
            if (seed_ip != local_ip || seed_port != local_port) {
                network.add_seed_node(seed_ip, seed_port);
                std::cout << "[App] Added seed node: " 
                          << seed_ip << ":" << seed_port << "\n";
            }
        }
    }

    // Start the mesh network
    if (!network.start()) {
        std::cerr << "[App] Failed to start mesh network.\n";
        return 1;
    }
    std::cout << "[App] Mesh network started on " 
              << network.get_local_node_id().id << "\n";

    // Register OS signals
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "\nCommands:\n"
              << "  broadcast <message>      Send broadcast to all peers\n"
              << "  unicast <ip:port> <msg>  Send unicast to a specific peer\n"
              << "  peers                     List connected peers\n"
              << "  quit                      Exit application\n"
              << "---------------------------------------------\n";

    // Input thread for interactive commands
    std::thread input_thread([&]() {
        std::string line;
        while (app_running.load()) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) {
                if (std::cin.eof()) {
                    std::cout << "EOF detected, shutting down...\n";
                    app_running = false;
                    if (network_ptr) network_ptr->stop();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (!app_running.load()) break;

            if (command == "quit") {
                app_running = false;
                if (network_ptr) network_ptr->stop();
                break;
            }
            else if (command == "broadcast") {
                std::string message;
                std::getline(iss, message);
                if (!message.empty() && message.front() == ' ')
                    message.erase(0, 1);

                if (!message.empty()) {
                    network.send_broadcast(message);
                    std::cout << "[App] Broadcast '" << message << "' sent.\n";
                } else {
                    std::cout << "[App] Broadcast message cannot be empty.\n";
                }
            }
            else if (command == "unicast") {
                std::string target_str, message_part;
                iss >> target_str;
                std::getline(iss, message_part);
                if (!message_part.empty() && message_part.front() == ' ')
                    message_part.erase(0, 1);

                auto colon_pos = target_str.find(':');
                if (colon_pos != std::string::npos && !message_part.empty()) {
                    std::string target_ip   = target_str.substr(0, colon_pos);
                    uint16_t    target_port = static_cast<uint16_t>(
                                                    std::stoi(target_str.substr(colon_pos + 1))
                                                );
                    NodeId target_node(target_ip, target_port);
                    if (network.send_unicast(target_node, message_part)) {
                        std::cout << "[App] Unicast to " 
                                  << target_node.id << " sent.\n";
                    } else {
                        std::cout << "[App] Failed to send unicast to " 
                                  << target_node.id << ". Peer not connected.\n";
                    }
                } else {
                    std::cout << "[App] Invalid unicast format. "
                                 "Use: unicast ip:port message\n";
                }
            }
            else if (command == "peers") {
                auto current_peers = network.get_connected_peers();
                std::cout << "[App] Connected peers (" 
                          << current_peers.size() << "):\n";
                if (current_peers.empty()) {
                    std::cout << "  (No peers connected)\n";
                } else {
                    for (const auto& peer : current_peers) {
                        std::cout << "  - " << peer.id << "\n";
                    }
                }
            }
            else {
                std::cout << "[App] Unknown command: " << command << "\n";
            }
        }
    });

    // Main loop
    while (app_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Ensure network is stopped
    if (network.is_running()) {
        network.stop();
    }

    // Join input thread
    if (input_thread.joinable()) {
        input_thread.join();
    }

    std::cout << "[App] Network stopped. Exiting.\n";
    return 0;
}
```

---

## 6. Running the Example

### 6.1 Start Three Nodes on localhost

* **Node A (no seeds)**

  ```bash
  ./example_app 127.0.0.1 9001
  ```

  * Node A will bind to `127.0.0.1:9001` and start UDP discovery.

* **Node B (seed = Node A)**

  ```bash
  ./example_app 127.0.0.1 9002 127.0.0.1:9001
  ```

  * Node B connects to Node A over TCP and also broadcasts via UDP.

* **Node C (rely on UDP discovery or seed = Node B)**

  ```bash
  ./example_app 127.0.0.1 9003
  ```

  *or*

  ```bash
  ./example_app 127.0.0.1 9003 127.0.0.1:9002
  ```

### 6.2 Sample Commands (from any node)

```
> peers
[App] Connected peers (2):
  - 127.0.0.1:9001
  - 127.0.0.1:9002

> broadcast Hello everyone!
[App] Broadcast 'Hello everyone!' sent.

> unicast 127.0.0.1:9002 Hello Node B from Node A!
[App] Unicast to 127.0.0.1:9002 sent.

> quit
[App] Network stopped. Exiting.
```

---

## 7. Use Cases

1. **Decentralized Chat**

   * Each node represents a chat client.
   * Use broadcast for public messages, unicast for private messages.

2. **Service Discovery**

   * Nodes announce available services (e.g., `"service": "image_processing"`).
   * Other nodes pick up and use these services dynamically.

3. **Collaborative Data Sharing**

   * A collaborative editor where changes are broadcast to all peers.
   * Each node applies JSON-formatted updates as they arrive.

4. **Distributed Task Queue**

   * A node broadcasts a task request.
   * Interested peers send a unicast to claim the task.
   * Results are sent back via unicast.

5. **Sensor Networks / IoT**

   * IoT devices form a local mesh.
   * Sensors broadcast readings; controllers unicast configuration commands.

---

## 8. Running the Test Suite

1. **Build Tests**

   ```bash
   make -f Makefile.tests
   ```

   or

   ```bash
   make -f Makefile.tests build_tests
   ```

2. **Run Tests**

   ```bash
   ./test_mesh_network
   ```

   * Tests include unicast/broadcast functionality, peer discovery, and node-failure recovery.

3. **Clean Test Builds**

   ```bash
   make -f Makefile.tests clean_tests
   ```

> **Note on Enhanced Test Suite**
> During development, an `enhanced_test_suite.cpp` was created with more rigorous scenarios:
>
> * Full-mesh validation under various seeding conditions.
> * Unicast/broadcast reliability under dynamic membership.
> * Scalability assessments.
>
> Review `enhanced_test_suite.cpp` and consider integrating its test cases into `test_mesh_network.cpp` to strengthen coverage.

---

## 9. Assumptions & Design Decisions

* **Flat Mesh Topology**: Each node maintains direct TCP connections to all known peers.
* **JSON Messaging**: All messages (heartbeats, peer lists, unicast/broadcast) use JsonCpp for serialization.
* **Peer Discovery**:

  * Initial discovery via UDP broadcast (no configuration required on LAN).
  * Transitive discovery by exchanging peer lists over TCP.
* **Heartbeating**: Periodic TCP-based heartbeats ensure timely detection of unresponsive peers.
* **No Multi-hop Routing**: The library focuses on a flat mesh; it does not implement multi-hop routing or end-to-end delivery guarantees beyond the TCP link.

---

## 10. Known Issues & Limitations

* **UDP Reliability**: UDP broadcasts are inherently unreliable. Missed broadcasts may delay initial discovery.
* **Broadcast Efficiency**: Broadcast messages are individually sent to each peer. In networks with many peers, this can become inefficient.
* **Network Partitions**: No advanced mechanism exists to detect or heal network partitions automatically.
* **Scalability**: For very large meshes, a full-mesh TCP model may not scale. Consider hierarchical or partial-mesh designs for hundreds of peers.
* **Test Coverage**:

  * UDP discovery logic is implemented, but comprehensive automated tests for this mechanism reside in `enhanced_test_suite.cpp`.
  * Integrating those tests into the main suite will improve overall coverage.

---

## 11. License


See `LICENSE` for details.

---

## 12. Contact & Contribution

* **Repository**: `<repository URL>`
* **Issues / Pull Requests**: Please open issues or PRs on GitLab/GitHub.
* **Author**: \[Your Name]
* **Acknowledgments**: Based on ZeroMQ, JsonCpp, and contributions from the open-source community.

```markdown
```
