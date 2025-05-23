# AV-PeerZMQ
AV-PeerZMQ: Peer-to-Peer Mesh Networking Library
1. Project Overview
AV-PeerZMQ is a C++ library designed for building decentralized, peer-to-peer (P2P) mesh networks. It leverages ZeroMQ for asynchronous messaging and JSONCpp for message serialization. The library enables nodes to discover each other, send unicast and broadcast messages, and maintain a resilient network topology.

Key Features:

Dynamic Peer Discovery:
Seed Nodes: Nodes can be bootstrapped by connecting to a list of known seed nodes. This provides a reliable way to join an existing mesh.
UDP Broadcast Discovery: Nodes can discover each other on the local network (LAN) via UDP broadcasts. This allows for zero-configuration setups in LAN environments where nodes can dynamically find each other without prior knowledge.
Messaging:
Unicast: Send targeted messages directly to a specific peer.
Broadcast: Propagate messages to all directly connected peers in the mesh.
Peer Management: Includes automatic heartbeating to monitor peer health, timeout detection for unresponsive peers, and dynamic connection management.
Callbacks: Provides an easy way for applications to react to network events by setting callbacks for received unicast and broadcast messages.
Resilience: The network is designed to handle nodes joining and leaving dynamically, with peers attempting to reconnect or find new peers if connections are lost.
Cross-Platform (Potentially): Built with standard C++ and common libraries like ZeroMQ and JsonCpp, making it potentially portable across different operating systems.
2. Architecture and Component Descriptions
The library revolves around a few core components:

MeshNetwork Class: This is the central class and the primary interface for applications. It encapsulates all networking logic, including peer discovery, message handling, and connection management.
ZeroMQ Sockets:
router_socket_ (ZMQ_ROUTER type): A TCP socket that listens on the node's main IP address and port. It's used for reliable, ordered communication with connected peers. This includes sending/receiving unicast messages, application-level broadcasts, heartbeats, and peer lists. The ROUTER socket allows multiple peers to connect to it.
discovery_socket_ (ZMQ_DGRAM type): A UDP socket bound to a specific discovery port (typically local_port + 1000 by default, but configurable). This socket is used for sending out UDP broadcast messages for local peer discovery and for receiving such broadcasts from other nodes.
Threads: MeshNetwork employs several background threads for concurrent operations:
message_handler_thread_: Polls both the router_socket_ (for TCP messages) and the discovery_socket_ (for UDP discovery broadcasts). Received messages are dispatched to appropriate internal handlers (e.g., handle_message for TCP, handle_discovery_message for UDP).
heartbeat_thread_: Periodically sends heartbeat messages to all currently connected peers to signal presence and maintain active connections.
discovery_thread_: Periodically attempts to connect to any configured seed nodes. It also triggers the UDP broadcast of discovery messages on the local network.
cleanup_thread_: Periodically checks for peers that have not sent a heartbeat within the timeout period and disconnects them.
NodeId Struct: A simple structure representing a unique node in the network. It contains the node's IP address (std::string ip), port (uint16_t port), and a concatenated string ID (std::string id in "ip:port" format) for easy identification and use in hash maps.
PeerInfo Struct: Stores runtime information about each connected peer. This includes the peer's NodeId, the timestamp of the last message received (last_seen), its connection status (is_connected), and a std::unique_ptr<zmq::socket_t> which is a ZMQ_DEALER socket used to send messages directly to that peer.
Message Types (Internal Protocol): An enum class MessageType defines the types of messages exchanged internally by MeshNetwork nodes:
HEARTBEAT: Indicates a node is still alive.
PEER_DISCOVERY: Used in UDP broadcasts to announce presence, or sent over TCP to request a list of known peers from an already connected peer.
UNICAST: An application-level message intended for a single target node.
BROADCAST: An application-level message to be disseminated to all connected peers.
PEER_LIST: A response to a PEER_DISCOVERY (TCP) request, containing a list of other peers known to the sender. This helps in transitive discovery.
GOODBYE: A message sent by a node when it is shutting down gracefully.
Message Format: All messages are JSON objects, serialized to strings for transmission. Each message typically includes:
"type": An integer corresponding to one of the MessageType enum values.
"data": A JSON object containing the payload. Common fields within data include:
"sender": The NodeId.id string of the originating node.
"timestamp": Message creation time.
Specific fields based on message type (e.g., "message" for UNICAST/BROADCAST, "peers" array for PEER_LIST).
Communication Flow Example (Simplified):

Node A Starts: Binds its TCP router socket and UDP discovery socket. Starts its background threads. If no seeds, it relies on UDP.
Node A Discovery Broadcast: Periodically, Node A's discovery_thread_ sends a PEER_DISCOVERY message via UDP broadcast. This message contains Node A's NodeId (IP and main listening port).
Node B Starts: Similarly binds sockets and starts threads.
Node B Receives UDP Discovery: Node B's message_handler_thread_ (polling the discovery_socket_) receives Node A's UDP broadcast. The handle_discovery_message function parses it.
Node B Connects to Node A: Node B calls connect_to_peer(NodeA_NodeId). This creates a ZMQ_DEALER socket on Node B that connects to Node A's ZMQ_ROUTER socket (TCP).
Peer List Exchange (Optional but common): Upon connection, nodes might exchange PEER_DISCOVERY (TCP) / PEER_LIST messages to learn about other peers transitively.
Heartbeating: Node A and Node B now periodically send HEARTBEAT messages to each other over their TCP link.
Application Messaging: Node A can now send_unicast(NodeB_NodeId, "Hello!") or send_broadcast("General Update!").
3. Building the Project
Dependencies:

ZeroMQ (libzmq): Version 4.x.x or higher is recommended. Handles the underlying socket communication.
JsonCpp (libjsoncpp): Used for parsing and generating JSON-formatted messages. Version 1.x.x is typical.
C++ Compiler: A compiler with support for C++11 features (threads, smart pointers, etc.) is required (e.g., GCC 4.8+, Clang 3.3+).
CMake (Optional but Recommended): While not strictly required if compiling manually, CMake is useful for managing builds, especially across different platforms. (A CMakeLists.txt is not provided in the current file set but would be a good addition).
Basic Build Commands (using g++ on Linux/macOS):

These commands assume that ZeroMQ and JsonCpp libraries and headers are installed in standard system locations or paths known to the compiler.

Compiling mesh_network (if it were a separate library):
# g++ -std=c++11 -Wall -c mesh_network.cpp -o mesh_network.o -I/usr/local/include -pthread
Compiling the example_app:
g++ -std=c++11 -Wall -o example_app example_app.cpp mesh_network.cpp -I/usr/local/include -L/usr/local/lib -lzmq -ljsoncpp -pthread
Compiling the test_mesh_network suite:
g++ -std=c++11 -Wall -o test_mesh_network test_mesh_network.cpp mesh_network.cpp -I/usr/local/include -L/usr/local/lib -lzmq -ljsoncpp -pthread
Notes:

Replace /usr/local/include and /usr/local/lib with the correct paths if your libraries are installed elsewhere (e.g., via a package manager like Homebrew on macOS, they might be in /opt/homebrew/...).
The -Wall flag enables common compiler warnings, which is good practice.
The -pthread flag is crucial for linking the POSIX threads library, necessary for std::thread.
4. Usage Instructions & Detailed Examples
Integrating the Library
To use MeshNetwork in your application:

Include mesh_network.h.
Compile mesh_network.cpp and link it with your application, along with ZeroMQ and JsonCpp libraries.
Core Usage Pattern
#include "mesh_network.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal> // For signal handling

// Global pointer to control the main loop and ensure cleanup
std::atomic<bool> app_running(true);
MeshNetwork* network_ptr = nullptr;

void signal_handler_main(int signum) {
    std::cout << "\nCaught signal " << signum << ". Shutting down..." << std::endl;
    app_running = false;
    if (network_ptr) {
        network_ptr->stop(); // Attempt graceful shutdown of network
    }
}

// --- Application-specific Callbacks ---
void my_unicast_handler(const NodeId& sender, const std::string& message) {
    std::cout << "[App] Unicast from " << sender.id << ": " << message << std::endl;
}

void my_broadcast_handler(const NodeId& sender, const std::string& message) {
    std::cout << "[App] Broadcast from " << sender.id << " ("
              << (sender.id == network_ptr->get_local_node_id().id ? "self" : "peer")
              << "): " << message << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <local_ip> <local_port> [seed_ip1:port1 seed_ip2:port2 ...]" << std::endl;
        std::cerr << "Example: ./my_app 127.0.0.1 9001" << std::endl;
        std::cerr << "Example with seeds: ./my_app 127.0.0.1 9002 127.0.0.1:9001" << std::endl;
        return 1;
    }

    std::string local_ip = argv[1];
    uint16_t local_port = static_cast<uint16_t>(std::stoi(argv[2]));

    // 1. Initialize MeshNetwork
    // You can also specify a discovery port, e.g., MeshNetwork network(local_ip, local_port, local_port + 10000);
    MeshNetwork network(local_ip, local_port);
    network_ptr = &network; // For signal handler

    // 2. Set Message Callbacks
    network.set_unicast_callback(my_unicast_handler);
    network.set_broadcast_callback(my_broadcast_handler);

    // 3. Add Seed Nodes (Optional)
    // Seed nodes help in finding initial peers, especially if UDP broadcast is restricted.
    for (int i = 3; i < argc; ++i) {
        std::string seed_address = argv[i];
        size_t colon_pos = seed_address.find(':');
        if (colon_pos != std::string::npos) {
            std::string seed_ip = seed_address.substr(0, colon_pos);
            uint16_t seed_port = static_cast<uint16_t>(std::stoi(seed_address.substr(colon_pos + 1)));
            if (seed_ip != local_ip || seed_port != local_port) { // Don't add self as seed
                 network.add_seed_node(seed_ip, seed_port);
                 std::cout << "[App] Added seed node: " << seed_ip << ":" << seed_port << std::endl;
            }
        }
    }

    // 4. Start the Network
    if (!network.start()) {
        std::cerr << "[App] Failed to start mesh network." << std::endl;
        return 1;
    }
    std::cout << "[App] Mesh network started on " << network.get_local_node_id().id << std::endl;
    // std::cout << "[App] Discovery port is " << network.get_discovery_port() << std::endl; // Assuming a getter for discovery_port_

    // Register signal handler for graceful shutdown (Ctrl+C)
    signal(SIGINT, signal_handler_main);
    signal(SIGTERM, signal_handler_main);

    // 5. Application Logic (Example: Simple CLI)
    std::cout << "\nAV-PeerZMQ Example CLI Running. Commands:" << std::endl;
    std::cout << "  broadcast <message>     - Send broadcast" << std::endl;
    std::cout << "  unicast <ip:port> <msg> - Send unicast" << std::endl;
    std::cout << "  peers                   - List connected peers" << std::endl;
    std::cout << "  quit                    - Exit application" << std::endl;
    std::cout << "---------------------------------------------" << std::endl;

    std::thread input_thread([&]() {
        std::string line;
        while (app_running) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) {
                if (std::cin.eof()) { // Handle EOF (Ctrl+D)
                    std::cout << "EOF detected, initiating shutdown..." << std::endl;
                    app_running = false;
                    if (network_ptr) network_ptr->stop(); // Ensure network stop is called
                    break;
                }
                // Other stream errors, might need a small delay or check app_running
                if (!app_running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (line.empty() || !app_running) continue;

            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (!app_running) break; // Check again after potential block in getline

            if (command == "quit") {
                app_running = false;
                if (network_ptr) network_ptr->stop(); // Ensure network stop is called
                break;
            } else if (command == "broadcast") {
                std::string message;
                std::getline(iss, message); // Read rest of the line
                if (!message.empty() && message[0] == ' ') message = message.substr(1); // Trim leading space
                if (!message.empty()) {
                    network.send_broadcast(message);
                    std::cout << "[App] Broadcast '" << message << "' sent." << std::endl;
                } else {
                    std::cout << "[App] Broadcast message cannot be empty." << std::endl;
                }
            } else if (command == "unicast") {
                std::string target_str, message_part;
                iss >> target_str;
                std::getline(iss, message_part);
                if (!message_part.empty() && message_part[0] == ' ') message_part = message_part.substr(1);

                size_t colon_pos = target_str.find(':');
                if (colon_pos != std::string::npos && !message_part.empty()) {
                    std::string target_ip = target_str.substr(0, colon_pos);
                    uint16_t target_port = static_cast<uint16_t>(std::stoi(target_str.substr(colon_pos + 1)));
                    NodeId target_node(target_ip, target_port);
                    if (network.send_unicast(target_node, message_part)) {
                         std::cout << "[App] Unicast to " << target_node.id << " sent." << std::endl;
                    } else {
                         std::cout << "[App] Failed to send unicast to " << target_node.id << " (peer not connected or unknown)." << std::endl;
                    }
                } else {
                    std::cout << "[App] Invalid unicast format. Use: unicast ip:port message" << std::endl;
                }
            } else if (command == "peers") {
                auto current_peers = network.get_connected_peers();
                std::cout << "[App] Connected peers (" << current_peers.size() << "):" << std::endl;
                if (current_peers.empty()) {
                    std::cout << "  (No peers connected)" << std::endl;
                } else {
                    for (const auto& peer : current_peers) {
                        std::cout << "  - " << peer.id << std::endl;
                    }
                }
            } else {
                std::cout << "[App] Unknown command: " << command << std::endl;
            }
        }
        // If loop exited due to cin error/eof not covered by app_running=false
        if (app_running) {
            app_running = false;
            if (network_ptr && network_ptr->is_running()) network_ptr->stop();
        }
        std::cout << "[App] Input thread finished." << std::endl;
    });

    // Keep main thread alive while app is running
    while(app_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Ensure network is stopped and input thread is joined
    if (network.is_running()) {
        std::cout << "[App] Main thread initiating final shutdown check..." << std::endl;
        network.stop(); // This is safe due to internal checks in stop()
    }

    // Ensure std::cin is unblocked for the input_thread to join if it's stuck on getline
    // This is a common issue with joining console input threads.
    // A more robust way might involve closing std::cin or using select/poll on cin.
    // For this example, if not already shut down by EOF or 'quit', this helps.
    // Note: This might not be perfectly clean on all OSes if input_thread is deeply blocked.
    if(input_thread.joinable()){
        std::cout << "[App] Ensuring input stream is clear for input thread to join..." << std::endl;
        // The following line is a platform-specific way to potentially unblock std::cin.
        // It's not guaranteed to work or be the best way.
        // On POSIX, one might try: close(STDIN_FILENO);
        // For this example, the signal handler and EOF check are primary shutdown paths for input_thread.
        // If they worked, input_thread would have already set app_running to false.
        // If main loop exited first (e.g. external kill not caught by signal), cin might still be blocking.
        // A robust app might use a condition variable or pipe to signal the input thread.
    }

    if(input_thread.joinable()) {
        input_thread.join();
    }

    std::cout << "[App] Network stopped. Application terminated." << std::endl;
    return 0;
}
example_app.cpp (Explanation)
The example_app.cpp included in the repository provides a ready-to-run command-line application that uses the MeshNetwork library. It allows you to start multiple nodes and have them interact.

How it works:

It takes its own IP and port as command-line arguments.
Optionally, you can provide IP:port addresses of other nodes to use as seeds.
Once started, it provides a simple command prompt (>) for:
broadcast <message>: Sends a message to all connected peers.
unicast <ip:port> <message>: Sends a message to a specific peer.
peers: Lists the NodeIds of currently connected peers.
quit or exit: Shuts down the node gracefully.
Running example_app (Example Scenario):

Compile: (As shown in "Building the Project" section)
g++ -std=c++11 -Wall -o example_app example_app.cpp mesh_network.cpp -I/usr/local/include -L/usr/local/lib -lzmq -ljsoncpp -pthread
Terminal 1 (Node A - First node, no seeds):
./example_app nodeA 127.0.0.1 9001
Node A will start and begin UDP discovery broadcasts.
Terminal 2 (Node B - Connects to Node A as a seed):
./example_app nodeB 127.0.0.1 9002 127.0.0.1:9001
Node B will start and attempt to connect to Node A. Once connected, they will exchange heartbeats. Node B will also start UDP discovery.
Terminal 3 (Node C - Relies on UDP discovery or seeds to B):
# Option 1: Rely on UDP discovery (Node A and B should be broadcasting)
./example_app nodeC 127.0.0.1 9003
# Option 2: Seed from Node B
# ./example_app nodeC 127.0.0.1 9003 127.0.0.1:9002
Node C should discover and connect to the other nodes.
Now, from any terminal, you can use the commands:

peers (to see who you are connected to)
broadcast Hello everyone!
unicast 127.0.0.1:9002 Hello Node B from Node A! (if sent from Node A to Node B)
5. Use Cases
AV-PeerZMQ can be a foundational library for various decentralized applications:

Decentralized Chat Applications: Nodes can join the mesh, and users can send messages. Broadcasts can be used for public room messages, while unicasts can be used for private messages between users on specific nodes. Each message could be a JSON object containing user, timestamp, and text.

Simple Service Discovery: Nodes can broadcast their available services or capabilities upon joining the network (e.g., "service_name": "image_processing", "status": "available"). Other nodes can listen for these broadcasts to dynamically find and utilize services offered by peers.

Collaborative Data Sharing / Synchronization: For applications where multiple users need to work on shared data (e.g., a collaborative editor, distributed whiteboards), AV-PeerZMQ can propagate changes or updates. A node making a change can broadcast it, and other nodes can update their local state. More complex state synchronization would require additional logic on top.

Lightweight Distributed Task Queues: A node can broadcast a task request. Interested and available nodes can pick up the task, possibly by sending a unicast message back to the requester to claim it. Results can then be sent back via unicast.

Sensor Networks / IoT: In a local network, IoT devices or sensors could form a mesh to relay data. For instance, a sensor might broadcast its readings, or a central node could unicast configuration commands to specific sensors.

6. How to Run the Tests
The test_mesh_network.cpp file provides a test suite.

Compile the test suite:

g++ -std=c++11 -Wall -o test_mesh_network test_mesh_network.cpp mesh_network.cpp -I/usr/local/include -L/usr/local/lib -lzmq -ljsoncpp -pthread
(Adjust include/library paths as necessary).

Run the tests:

./test_mesh_network
The test suite executes various scenarios, including unicast/broadcast functionality, peer discovery (both seed-based and UDP if tests were successfully updated), and node failure recovery. Test results are printed to the console.

Note on UDP Discovery Tests: While the core UDP discovery reception logic is implemented in the library, the full enhancement and verification of tests for this specific mechanism within test_mesh_network.cpp were significantly hindered by persistent issues with my file editing capabilities. Therefore, the test coverage for UDP-only discovery scenarios might be limited or may rely on older test structures.

7. Assumptions and Design Decisions
JSON for Messaging: Chosen for human readability and ease of parsing, suitable for many applications. For high-performance or bandwidth-constrained scenarios, a binary serialization format might be preferred.
ZeroMQ Socket Choices:
ZMQ_ROUTER: For the main listening socket, allowing robust handling of many incoming peer connections.
ZMQ_DEALER: For outgoing connections from a PeerInfo object to a peer's ROUTER.
ZMQ_DGRAM: For connectionless UDP broadcasts, suitable for local network discovery.
Threading Model: A thread-per-task model (message handling, heartbeating, discovery, cleanup) is used for simplicity and modularity. For very large numbers of nodes or extreme performance, an event-driven model with fewer threads might be considered.
Discovery Scope: UDP broadcast discovery is primarily for LAN environments. Seed nodes are crucial for bootstrapping connections across different networks or when UDP broadcasts are filtered.
No Encryption/Security: The library does not implement any message encryption or node authentication. For production use in untrusted environments, security features would need to be added (e.g., using ZMQ's built-in CURVE security or an application-layer solution).
Error Handling: Errors are generally logged to std::cerr. Production applications would likely require more sophisticated logging and error management.
8. Known Issues and Limitations
UDP Discovery Test Coverage: As noted, while the UDP discovery reception logic is now part of the main codebase (mesh_network.h and mesh_network.cpp), my ability to fully update test_mesh_network.cpp to comprehensively test this feature was blocked by persistent failures during this phase of development. The existing tests primarily validate seed-node based discovery and core messaging.
Scalability of Broadcasts: Broadcast messages are sent individually to each connected peer. This could be inefficient in networks with a very large number of direct peers for a single node.
Network Partitions: The library does not have advanced mechanisms to detect or automatically heal network partitions.
Message Guarantees: While TCP provides reliability for direct peer-to-peer links, the library itself does not offer end-to-end guaranteed delivery or complex routing across multiple hops (it primarily facilitates a flat mesh of directly connected peers).
UDP Reliability: UDP discovery messages are inherently unreliable and can be lost. The periodic nature of these broadcasts and the seed node mechanism are intended to mitigate this for initial discovery.
