#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

#define CPPZMQ_HAS_DRAFT_API // Enable DRAFT API for ZMQ_DGRAM if needed
// #include <zmq.h> // Let zmq.hpp include zmq.h
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <random>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

// Lightweight logging macros
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
// #define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl // Example for conditional debug

// Message types for internal protocol
enum class MessageType : uint8_t {
    HEARTBEAT = 1,
    PEER_DISCOVERY = 2,
    UNICAST = 3,
    BROADCAST = 4,
    PEER_LIST = 5,
    GOODBYE = 6
};

// Node identifier structure
struct NodeId {
    std::string ip;
    uint16_t port;
    std::string id;  // Unique identifier combining ip:port
    
    NodeId() = default;
    NodeId(const std::string& ip, uint16_t port) 
        : ip(ip), port(port), id(ip + ":" + std::to_string(port)) {}
    
    bool operator==(const NodeId& other) const {
        return id == other.id;
    }
    
    bool operator<(const NodeId& other) const {
        return id < other.id;
    }
};

// Hash function for NodeId to use in unordered containers
struct NodeIdHash {
    std::size_t operator()(const NodeId& node) const {
        return std::hash<std::string>{}(node.id);
    }
};

// Peer information structure
struct PeerInfo {
    NodeId node_id;
    std::chrono::steady_clock::time_point last_seen;
    bool is_connected;
    std::unique_ptr<zmq::socket_t> socket;
    
    PeerInfo(const NodeId& id) 
        : node_id(id), last_seen(std::chrono::steady_clock::now()), is_connected(false) {}
};

// Message callback types
using UnicastCallback = std::function<void(const NodeId&, const std::string&)>;
using BroadcastCallback = std::function<void(const NodeId&, const std::string&)>;

class MeshNetwork {
private:
    // Core ZeroMQ context and sockets
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> router_socket_;  // For receiving messages
    std::unique_ptr<zmq::socket_t> discovery_socket_;  // For peer discovery
    
    // Node identification
    NodeId local_node_;
    
    // Peer management
    std::unordered_map<NodeId, std::unique_ptr<PeerInfo>, NodeIdHash> peers_;
    mutable std::mutex peers_mutex_; // Added mutable
    
    // Thread management
    std::atomic<bool> running_;
    std::thread message_handler_thread_;
    std::thread heartbeat_thread_;
    std::thread discovery_thread_;
    std::thread cleanup_thread_;
    
public: // Made public for test access to constants and general utility
    // Configuration Constants
    static constexpr int HEARTBEAT_INTERVAL_MS = 5000;
    static constexpr int PEER_TIMEOUT_MS = 15000;
    static constexpr int DISCOVERY_INTERVAL_MS = 10000;
    static constexpr int CLEANUP_INTERVAL_MS = 30000;
private: // Return to private for other members

    // Discovery configuration
    std::vector<NodeId> seed_nodes_;  // Bootstrap nodes for discovery
    uint16_t discovery_port_;
    
    // Message callbacks
    UnicastCallback unicast_callback_;
    BroadcastCallback broadcast_callback_;
    
    // Random number generator for jitter
    std::mt19937 rng_;
    
public:
    MeshNetwork(const std::string& local_ip, uint16_t local_port,
                uint16_t discovery_port = 0);
    ~MeshNetwork();
    
    // Core lifecycle methods
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Configuration methods
    void add_seed_node(const std::string& ip, uint16_t port);
    void set_unicast_callback(UnicastCallback callback);
    void set_broadcast_callback(BroadcastCallback callback);
    
    // Messaging methods
    bool send_unicast(const NodeId& target, const std::string& message);
    bool send_broadcast(const std::string& message);
    
    // Network information
    std::vector<NodeId> get_connected_peers() const;
    size_t get_peer_count() const;
    
private:
    // Internal message handling
    void message_handler_loop();
    void handle_message(const zmq::message_t& identity, const zmq::message_t& message);
    void process_heartbeat(const NodeId& sender, const nlohmann::json& data);
    void process_peer_discovery(const NodeId& sender, const nlohmann::json& data);
    void process_unicast(const NodeId& sender, const nlohmann::json& data);
    void process_broadcast(const NodeId& sender, const nlohmann::json& data);
    void process_peer_list(const NodeId& sender, const nlohmann::json& data);
    void process_goodbye(const NodeId& sender, const nlohmann::json& data);
    
    // Peer management
    void heartbeat_loop();
    void discovery_loop();
    void cleanup_loop();
    void connect_to_peer(const NodeId& peer_id);
    void disconnect_peer(const NodeId& peer_id);
    void update_peer_last_seen(const NodeId& peer_id);
    
    // Utility methods
    std::string create_message(MessageType type, const nlohmann::json& data);
    nlohmann::json parse_message(const std::string& message, MessageType& type, std::string& out_errors); // Declaration only
    std::string node_id_to_address(const NodeId& node_id);
    NodeId address_to_node_id(const std::string& address);
    void add_jitter(int base_ms);
    
    // Discovery methods
    void send_discovery_request();
    void send_peer_list(const NodeId& requester);
    void broadcast_discovery();
    void handle_discovery_message(const zmq::message_t& message);
};

// Implementation of these methods will be in mesh_network.cpp
// This is the core structure - the remaining methods follow similar patterns

#endif // MESH_NETWORK_H