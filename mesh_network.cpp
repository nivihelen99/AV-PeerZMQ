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

void MeshNetwork::handle_discovery_message(const zmq::message_t& message) {
    try {
        std::string msg_str(static_cast<char*>(message.data()), message.size());
        MessageType type;
        Json::Value data = parse_message(msg_str, type); // parse_message is an existing method

        if (data.empty()) {
            // parse_message logs errors or returns empty on failure.
            // Add minimal log here if needed, e.g., for debugging discovery issues.
            // std::cerr << "handle_discovery_message: Parsed data is empty." << std::endl;
            return;
        }

        if (type == MessageType::PEER_DISCOVERY) {
            if (!data.isMember("sender") || !data["sender"].isString() ||
                !data.isMember("port") || !data["port"].isUInt()) {
                std::cerr << "handle_discovery_message: PEER_DISCOVERY message missing or invalid sender/port fields." << std::endl;
                return;
            }
            
            std::string sender_id_str = data["sender"].asString();
            // address_to_node_id is an existing method that parses "ip:port" string to NodeId.
            NodeId discovered_peer_node = address_to_node_id(sender_id_str);

            if (discovered_peer_node.id.empty()) {
                // address_to_node_id returns an empty/invalid NodeId on parsing failure.
                std::cerr << "handle_discovery_message: Failed to parse NodeId from PEER_DISCOVERY message sender: " << sender_id_str << std::endl;
                return;
            }

            if (discovered_peer_node.id == local_node_.id) {
                return; // It's our own broadcast, ignore.
            }

            // Optional: Log successful reception for debugging.
            // std::cout << "handle_discovery_message: Received PEER_DISCOVERY via UDP from " << discovered_peer_node.id << std::endl;
            
            connect_to_peer(discovered_peer_node); // connect_to_peer is an existing method.
            
            // After connecting, send a PEER_DISCOVERY message back to the discovered peer
            // This allows the other peer to recognize us, connect back if needed, and exchange peer lists.
            // This helps in actively building the mesh network.
            Json::Value discovery_data;
            discovery_data["sender"] = local_node_.id;
            discovery_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::string discovery_msg_payload = create_message(MessageType::PEER_DISCOVERY, discovery_data);

            // We need to find the socket associated with this peer to send the message.
            // connect_to_peer should have established it.
            // It's crucial that this send doesn't block indefinitely or fail silently if the connection isn't ready.
            // The send_unicast method handles looking up the peer and its socket.
            // However, send_unicast creates a JSON message with a "message" field.
            // We need to send a raw PEER_DISCOVERY message.
            // Let's adapt the logic from send_unicast or send_peer_list for sending a specific message type.

            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(discovered_peer_node);
            if (it != peers_.end() && it->second->is_connected && it->second->socket) {
                try {
                    zmq::message_t msg(discovery_msg_payload.size());
                    memcpy(msg.data(), discovery_msg_payload.c_str(), discovery_msg_payload.size());
                    it->second->socket->send(msg, zmq::send_flags::dontwait);
                    // std::cout << "Sent PEER_DISCOVERY back to " << discovered_peer_node.id << " after UDP discovery." << std::endl;
                } catch (const zmq::error_t& e) {
                    // Log if sending the reciprocal discovery message fails. This is not critical but good for diagnostics.
                    std::cerr << "Failed to send PEER_DISCOVERY back to " << discovered_peer_node.id
                              << " after UDP discovery: " << e.what() << std::endl;
                }
            } else {
                // This might happen if connect_to_peer failed or is asynchronous and not yet complete.
                // std::cerr << "Could not send PEER_DISCOVERY back to " << discovered_peer_node.id
                //           << " after UDP discovery: Peer not found or not connected." << std::endl;
            }
            
        } else {
            // Optional: Log if other unexpected message types are received on the discovery port.
            // std::cerr << "handle_discovery_message: Received non-PEER_DISCOVERY message on discovery socket. Type: "
            //           << static_cast<int>(type) << std::endl;
        }
    } catch (const Json::Exception& e) {
        std::cerr << "handle_discovery_message: JSON parsing error: " << e.what() << " Message: " << std::string(static_cast<const char*>(message.data()), message.size()) << std::endl;
    } catch (const zmq::error_t& e) {
        // Avoid error spam if socket is closed during shutdown or resource temporarily unavailable.
        if (running_.load() && e.num() != ETERM && e.num() != EAGAIN) {
             std::cerr << "handle_discovery_message: ZMQ error: " << e.what() << std::endl;
        }
    } catch (const std::exception& e) {
         // Avoid error spam on shutdown.
         if (running_.load()) {
            std::cerr << "handle_discovery_message: Standard error: " << e.what() << std::endl;
        }
    }
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