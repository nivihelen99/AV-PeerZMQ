// mesh_network.cpp - Complete implementation of remaining methods

#include "mesh_network.h"
#include <algorithm>
#include <iomanip>

void MeshNetwork::process_heartbeat(const NodeId& sender, const Json::Value& data) {
    // Heartbeat received - peer is alive
    connect_to_peer(sender);
}

void MeshNetwork::process_peer_discovery(const NodeId& sender, const Json::Value& data) {
    // Peer discovery request - respond with our peer list
    send_peer_list(sender);
    connect_to_peer(sender);
}

void MeshNetwork::process_unicast(const NodeId& sender, const Json::Value& data) {
    if (unicast_callback_) {
        std::string message = data["message"].asString();
        unicast_callback_(sender, message);
    }
}

void MeshNetwork::process_broadcast(const NodeId& sender, const Json::Value& data) {
    if (broadcast_callback_) {
        std::string message = data["message"].asString();
        broadcast_callback_(sender, message);
    }
}

void MeshNetwork::process_peer_list(const NodeId& sender, const Json::Value& data) {
    if (data.isMember("peers") && data["peers"].isArray()) {
        for (const auto& peer_json : data["peers"]) {
            std::string peer_id = peer_json.asString();
            if (peer_id != local_node_.id) {
                NodeId peer_node = address_to_node_id(peer_id);
                connect_to_peer(peer_node);
            }
        }
    }
}

void MeshNetwork::process_goodbye(const NodeId& sender, const Json::Value& data) {
    std::cout << "Received goodbye from " << sender.id << std::endl;
    disconnect_peer(sender);
}

void MeshNetwork::heartbeat_loop() {
    while (running_.load()) {
        // Send heartbeat to all connected peers
        Json::Value heartbeat_data;
        heartbeat_data["sender"] = local_node_.id;
        heartbeat_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        std::string heartbeat_msg = create_message(MessageType::HEARTBEAT, heartbeat_data);
        
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (auto& pair : peers_) {
                if (pair.second->is_connected && pair.second->socket) {
                    try {
                        zmq::message_t msg(heartbeat_msg.size());
                        memcpy(msg.data(), heartbeat_msg.c_str(), heartbeat_msg.size());
                        pair.second->socket->send(msg, zmq::send_flags::dontwait);
                    } catch (const zmq::error_t& e) {
                        std::cerr << "Failed to send heartbeat to " << pair.first.id 
                                 << ": " << e.what() << std::endl;
                        pair.second->is_connected = false;
                    }
                }
            }
        }
        
        add_jitter(HEARTBEAT_INTERVAL_MS);
    }
}

void MeshNetwork::discovery_loop() {
    while (running_.load()) {
        // Try to connect to seed nodes
        for (const auto& seed : seed_nodes_) {
            connect_to_peer(seed);
        }
        
        // Send discovery broadcast
        broadcast_discovery();
        
        add_jitter(DISCOVERY_INTERVAL_MS);
    }
}

void MeshNetwork::cleanup_loop() {
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        std::vector<NodeId> to_disconnect;
        
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (auto& pair : peers_) {
                auto time_since_last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - pair.second->last_seen).count();
                
                if (time_since_last_seen > PEER_TIMEOUT_MS) {
                    to_disconnect.push_back(pair.first);
                }
            }
        }
        
        // Disconnect timed-out peers
        for (const auto& peer_id : to_disconnect) {
            std::cout << "Peer " << peer_id.id << " timed out, disconnecting" << std::endl;
            disconnect_peer(peer_id);
        }
        
        add_jitter(CLEANUP_INTERVAL_MS);
    }
}

void MeshNetwork::connect_to_peer(const NodeId& peer_id) {
    if (peer_id.id == local_node_.id) {
        return; // Don't connect to ourselves
    }
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(peer_id);
    if (it != peers_.end() && it->second->is_connected) {
        // Already connected
        it->second->last_seen = std::chrono::steady_clock::now();
        return;
    }
    
    try {
        // Create or update peer info
        if (it == peers_.end()) {
            peers_[peer_id] = std::make_unique<PeerInfo>(peer_id);
            it = peers_.find(peer_id);
        }
        
        // Create new socket for this peer
        it->second->socket = std::make_unique<zmq::socket_t>(context_, ZMQ_DEALER);
        
        // Set socket identity for routing
        std::string identity = local_node_.id;
        it->second->socket->set(zmq::sockopt::routing_id, identity);
        
        // Set socket options
        int linger = 0;
        it->second->socket->set(zmq::sockopt::linger, linger);
        
        // Connect to peer
        std::string peer_address = node_id_to_address(peer_id);
        it->second->socket->connect(peer_address);
        
        it->second->is_connected = true;
        it->second->last_seen = std::chrono::steady_clock::now();
        
        std::cout << "Connected to peer: " << peer_id.id << std::endl;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to connect to peer " << peer_id.id << ": " << e.what() << std::endl;
        if (it != peers_.end()) {
            it->second->is_connected = false;
            it->second->socket.reset();
        }
    }
}

void MeshNetwork::disconnect_peer(const NodeId& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        if (it->second->socket) {
            it->second->socket->close();
            it->second->socket.reset();
        }
        it->second->is_connected = false;
        std::cout << "Disconnected from peer: " << peer_id.id << std::endl;
    }
}

void MeshNetwork::update_peer_last_seen(const NodeId& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        it->second->last_seen = std::chrono::steady_clock::now();
    }
}

void MeshNetwork::send_discovery_request() {
    Json::Value discovery_data;
    discovery_data["sender"] = local_node_.id;
    discovery_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string discovery_msg = create_message(MessageType::PEER_DISCOVERY, discovery_data);
    
    // Send to all known peers
    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (auto& pair : peers_) {
        if (pair.second->is_connected && pair.second->socket) {
            try {
                zmq::message_t msg(discovery_msg.size());
                memcpy(msg.data(), discovery_msg.c_str(), discovery_msg.size());
                pair.second->socket->send(msg, zmq::send_flags::dontwait);
            } catch (const zmq::error_t& e) {
                std::cerr << "Failed to send discovery request to " << pair.first.id 
                         << ": " << e.what() << std::endl;
            }
        }
    }
}

void MeshNetwork::send_peer_list(const NodeId& requester) {
    Json::Value peer_list_data;
    peer_list_data["sender"] = local_node_.id;
    
    Json::Value peers_array(Json::arrayValue);
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto& pair : peers_) {
            if (pair.second->is_connected) {
                peers_array.append(pair.first.id);
            }
        }
    }
    
    peer_list_data["peers"] = peers_array;
    peer_list_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string peer_list_msg = create_message(MessageType::PEER_LIST, peer_list_data);
    
    // Find the requester and send the peer list
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(requester);
    if (it != peers_.end() && it->second->is_connected && it->second->socket) {
        try {
            zmq::message_t msg(peer_list_msg.size());
            memcpy(msg.data(), peer_list_msg.c_str(), peer_list_msg.size());
            it->second->socket->send(msg, zmq::send_flags::dontwait);
        } catch (const zmq::error_t& e) {
            std::cerr << "Failed to send peer list to " << requester.id 
                     << ": " << e.what() << std::endl;
        }
    }
}

void MeshNetwork::broadcast_discovery() {
    // Use UDP broadcast for initial peer discovery
    Json::Value discovery_data;
    discovery_data["sender"] = local_node_.id;
    discovery_data["port"] = local_node_.port;
    discovery_data["discovery_port"] = discovery_port_;
    discovery_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string discovery_msg = create_message(MessageType::PEER_DISCOVERY, discovery_data);
    
    try {
        // Broadcast to local network (this is a simplified approach)
        // In production, you might want to use multicast or a more sophisticated discovery mechanism
        std::string broadcast_address = "udp://255.255.255.255:" + std::to_string(discovery_port_);
        
        zmq::socket_t broadcast_socket(context_, ZMQ_DGRAM);
        broadcast_socket.connect(broadcast_address);
        
        zmq::message_t msg(discovery_msg.size());
        memcpy(msg.data(), discovery_msg.c_str(), discovery_msg.size());
        broadcast_socket.send(msg, zmq::send_flags::dontwait);
        
        broadcast_socket.close();
        
    } catch (const zmq::error_t& e) {
        // Broadcast might fail in some network configurations - that's okay
        // We rely on seed nodes for bootstrap
    }
}

std::string MeshNetwork::node_id_to_address(const NodeId& node_id) {
    return "tcp://" + node_id.ip + ":" + std::to_string(node_id.port);
}

NodeId MeshNetwork::address_to_node_id(const std::string& address) {
    // Parse address in format "ip:port"
    size_t colon_pos = address.find_last_of(':');
    if (colon_pos != std::string::npos) {
        std::string ip = address.substr(0, colon_pos);
        uint16_t port = static_cast<uint16_t>(std::stoi(address.substr(colon_pos + 1)));
        return NodeId(ip, port);
    }
    return NodeId();
}

void MeshNetwork::add_jitter(int base_ms) {
    // Add random jitter to prevent thundering herd
    std::uniform_int_distribution<int> dist(base_ms * 0.8, base_ms * 1.2);
    int sleep_ms = dist(rng_);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
}