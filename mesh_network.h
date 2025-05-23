#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

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
#include <json/json.h>
#include <iostream>
#include <sstream>

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
    std::mutex peers_mutex_;
    
    // Thread management
    std::atomic<bool> running_;
    std::thread message_handler_thread_;
    std::thread heartbeat_thread_;
    std::thread discovery_thread_;
    std::thread cleanup_thread_;
    
    // Configuration
    static constexpr int HEARTBEAT_INTERVAL_MS = 5000;
    static constexpr int PEER_TIMEOUT_MS = 15000;
    static constexpr int DISCOVERY_INTERVAL_MS = 10000;
    static constexpr int CLEANUP_INTERVAL_MS = 30000;
    
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
    void process_heartbeat(const NodeId& sender, const Json::Value& data);
    void process_peer_discovery(const NodeId& sender, const Json::Value& data);
    void process_unicast(const NodeId& sender, const Json::Value& data);
    void process_broadcast(const NodeId& sender, const Json::Value& data);
    void process_peer_list(const NodeId& sender, const Json::Value& data);
    void process_goodbye(const NodeId& sender, const Json::Value& data);
    
    // Peer management
    void heartbeat_loop();
    void discovery_loop();
    void cleanup_loop();
    void connect_to_peer(const NodeId& peer_id);
    void disconnect_peer(const NodeId& peer_id);
    void update_peer_last_seen(const NodeId& peer_id);
    
    // Utility methods
    std::string create_message(MessageType type, const Json::Value& data);
    Json::Value parse_message(const std::string& message, MessageType& type);
    std::string node_id_to_address(const NodeId& node_id);
    NodeId address_to_node_id(const std::string& address);
    void add_jitter(int base_ms);
    
    // Discovery methods
    void send_discovery_request();
    void send_peer_list(const NodeId& requester);
    void broadcast_discovery();
};

// Implementation begins here

MeshNetwork::MeshNetwork(const std::string& local_ip, uint16_t local_port, 
                        uint16_t discovery_port)
    : context_(1), local_node_(local_ip, local_port), running_(false),
      discovery_port_(discovery_port ? discovery_port : local_port + 1000),
      rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
    
    // Initialize router socket for main communication
    router_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_ROUTER);
    
    // Initialize discovery socket (UDP-like for broadcast discovery)
    discovery_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_DGRAM);
}

MeshNetwork::~MeshNetwork() {
    stop();
}

bool MeshNetwork::start() {
    if (running_.load()) {
        return false;
    }
    
    try {
        // Bind router socket
        std::string router_address = "tcp://*:" + std::to_string(local_node_.port);
        router_socket_->bind(router_address);
        
        // Bind discovery socket
        std::string discovery_address = "udp://*:" + std::to_string(discovery_port_);
        discovery_socket_->bind(discovery_address);
        
        // Set socket options
        int linger = 0;
        router_socket_->set(zmq::sockopt::linger, linger);
        discovery_socket_->set(zmq::sockopt::linger, linger);
        
        // Start worker threads
        running_.store(true);
        message_handler_thread_ = std::thread(&MeshNetwork::message_handler_loop, this);
        heartbeat_thread_ = std::thread(&MeshNetwork::heartbeat_loop, this);
        discovery_thread_ = std::thread(&MeshNetwork::discovery_loop, this);
        cleanup_thread_ = std::thread(&MeshNetwork::cleanup_loop, this);
        
        std::cout << "MeshNetwork started on " << local_node_.id << std::endl;
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to start MeshNetwork: " << e.what() << std::endl;
        running_.store(false);
        return false;
    }
}

void MeshNetwork::stop() {
    if (!running_.load()) {
        return;
    }
    
    // Send goodbye messages to all connected peers
    Json::Value goodbye_data;
    goodbye_data["node_id"] = local_node_.id;
    goodbye_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string goodbye_msg = create_message(MessageType::GOODBYE, goodbye_data);
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& pair : peers_) {
            if (pair.second->is_connected && pair.second->socket) {
                try {
                    zmq::message_t msg(goodbye_msg.size());
                    memcpy(msg.data(), goodbye_msg.c_str(), goodbye_msg.size());
                    pair.second->socket->send(msg, zmq::send_flags::dontwait);
                } catch (...) {
                    // Ignore errors during shutdown
                }
            }
        }
    }
    
    // Stop threads
    running_.store(false);
    
    if (message_handler_thread_.joinable()) {
        message_handler_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    // Close sockets
    router_socket_->close();
    discovery_socket_->close();
    
    // Clear peers
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        peers_.clear();
    }
    
    std::cout << "MeshNetwork stopped" << std::endl;
}

void MeshNetwork::add_seed_node(const std::string& ip, uint16_t port) {
    NodeId seed_node(ip, port);
    if (seed_node.id != local_node_.id) {
        seed_nodes_.push_back(seed_node);
    }
}

void MeshNetwork::set_unicast_callback(UnicastCallback callback) {
    unicast_callback_ = callback;
}

void MeshNetwork::set_broadcast_callback(BroadcastCallback callback) {
    broadcast_callback_ = callback;
}

bool MeshNetwork::send_unicast(const NodeId& target, const std::string& message) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(target);
    if (it == peers_.end() || !it->second->is_connected || !it->second->socket) {
        return false;
    }
    
    Json::Value data;
    data["sender"] = local_node_.id;
    data["target"] = target.id;
    data["message"] = message;
    data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string msg = create_message(MessageType::UNICAST, data);
    
    try {
        zmq::message_t zmq_msg(msg.size());
        memcpy(zmq_msg.data(), msg.c_str(), msg.size());
        it->second->socket->send(zmq_msg, zmq::send_flags::dontwait);
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to send unicast to " << target.id << ": " << e.what() << std::endl;
        return false;
    }
}

bool MeshNetwork::send_broadcast(const std::string& message) {
    Json::Value data;
    data["sender"] = local_node_.id;
    data["message"] = message;
    data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string msg = create_message(MessageType::BROADCAST, data);
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    bool success = false;
    
    for (auto& pair : peers_) {
        if (pair.second->is_connected && pair.second->socket) {
            try {
                zmq::message_t zmq_msg(msg.size());
                memcpy(zmq_msg.data(), msg.c_str(), msg.size());
                pair.second->socket->send(zmq_msg, zmq::send_flags::dontwait);
                success = true;
            } catch (const zmq::error_t& e) {
                std::cerr << "Failed to send broadcast to " << pair.first.id 
                         << ": " << e.what() << std::endl;
            }
        }
    }
    
    return success;
}

std::vector<NodeId> MeshNetwork::get_connected_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::vector<NodeId> connected_peers;
    
    for (const auto& pair : peers_) {
        if (pair.second->is_connected) {
            connected_peers.push_back(pair.first);
        }
    }
    
    return connected_peers;
}

size_t MeshNetwork::get_peer_count() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    size_t count = 0;
    for (const auto& pair : peers_) {
        if (pair.second->is_connected) {
            count++;
        }
    }
    return count;
}

void MeshNetwork::message_handler_loop() {
    zmq::pollitem_t items[] = {
        { router_socket_->handle(), 0, ZMQ_POLLIN, 0 }
    };
    
    while (running_.load()) {
        try {
            zmq::poll(items, 1, std::chrono::milliseconds(100));
            
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t identity, message;
                
                if (router_socket_->recv(identity, zmq::recv_flags::dontwait) &&
                    router_socket_->recv(message, zmq::recv_flags::dontwait)) {
                    handle_message(identity, message);
                }
            }
        } catch (const zmq::error_t& e) {
            if (running_.load()) {
                std::cerr << "Message handler error: " << e.what() << std::endl;
            }
        }
    }
}

void MeshNetwork::handle_message(const zmq::message_t& identity, const zmq::message_t& message) {
    try {
        std::string msg_str(static_cast<char*>(message.data()), message.size());
        MessageType type;
        Json::Value data = parse_message(msg_str, type);
        
        if (data.empty()) {
            return;
        }
        
        NodeId sender = address_to_node_id(data["sender"].asString());
        update_peer_last_seen(sender);
        
        switch (type) {
            case MessageType::HEARTBEAT:
                process_heartbeat(sender, data);
                break;
            case MessageType::PEER_DISCOVERY:
                process_peer_discovery(sender, data);
                break;
            case MessageType::UNICAST:
                process_unicast(sender, data);
                break;
            case MessageType::BROADCAST:
                process_broadcast(sender, data);
                break;
            case MessageType::PEER_LIST:
                process_peer_list(sender, data);
                break;
            case MessageType::GOODBYE:
                process_goodbye(sender, data);
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
}

std::string MeshNetwork::create_message(MessageType type, const Json::Value& data) {
    Json::Value message;
    message["type"] = static_cast<uint8_t>(type);
    message["data"] = data;
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, message);
}

Json::Value MeshNetwork::parse_message(const std::string& message, MessageType& type) {
    try {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        std::istringstream iss(message);
        if (Json::parseFromStream(builder, iss, &root, &errors)) {
            type = static_cast<MessageType>(root["type"].asUInt());
            return root["data"];
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse message: " << e.what() << std::endl;
    }
    
    return Json::Value();
}

// Additional implementation methods would continue here...
// This is the core structure - the remaining methods follow similar patterns

#endif // MESH_NETWORK_H