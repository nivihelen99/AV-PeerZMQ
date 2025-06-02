// mesh_network.cpp - Complete implementation of remaining methods

#include "mesh_network.h"
#include <nlohmann/json.hpp> // Already in mesh_network.h, but good for explicitness
#include <algorithm>
#include <iomanip>

void MeshNetwork::process_heartbeat(const NodeId& sender, const nlohmann::json& data) {
    // Heartbeat received - peer is alive
    connect_to_peer(sender);
}

void MeshNetwork::process_peer_discovery(const NodeId& sender, const nlohmann::json& data) {
    // Peer discovery request - respond with our peer list
    send_peer_list(sender);
    connect_to_peer(sender);
}

void MeshNetwork::process_unicast(const NodeId& sender, const nlohmann::json& data) {
    if (unicast_callback_) {
        std::string message = data["message"].get<std::string>();
        unicast_callback_(sender, message);
    }
}

void MeshNetwork::process_broadcast(const NodeId& sender, const nlohmann::json& data) {
    if (broadcast_callback_) {
        std::string message = data["message"].get<std::string>();
        broadcast_callback_(sender, message);
    }
}

void MeshNetwork::process_peer_list(const NodeId& sender, const nlohmann::json& data) {
    if (data.contains("peers") && data["peers"].is_array()) {
        for (const auto& peer_json : data["peers"]) {
            std::string peer_id = peer_json.get<std::string>();
            if (peer_id != local_node_.id) {
                NodeId peer_node = address_to_node_id(peer_id);
                connect_to_peer(peer_node);
            }
        }
    }
}

void MeshNetwork::process_goodbye(const NodeId& sender, const nlohmann::json& data) {
    LOG_INFO("Received goodbye from " << sender.id);
    disconnect_peer(sender);
}

void MeshNetwork::handle_discovery_message(const zmq::message_t& message) {
    std::string msg_str; // Declare msg_str outside the try block
    try {
        // Convert ZMQ message to std::string for parsing
        msg_str = std::string(static_cast<const char*>(message.data()), message.size());
        MessageType type;
        std::string parse_err_str; // Will capture parsing error details
        nlohmann::json data = parse_message(msg_str, type, parse_err_str);

        if (data.is_null()) { // Check for null JSON object
            LOG_ERROR("handle_discovery_message: Failed to parse discovery message. Error: " << parse_err_str
                      << ". Raw message (approx first 100 chars): " << msg_str.substr(0, 100));
            return;
        }

        if (type == MessageType::PEER_DISCOVERY) {
            // Check for essential fields after successful parsing
            if (!data.contains("sender") || !data["sender"].is_string() ||
                !data.contains("port") || !data["port"].is_number_unsigned()) { // Assuming 'port' for main comm port is still sent in discovery
                LOG_ERROR("handle_discovery_message: PEER_DISCOVERY message missing or invalid sender/port fields. Data: " << data.dump(4));
                return;
            }
            
            std::string sender_id_str = data["sender"].get<std::string>();
            // address_to_node_id is an existing method that parses "ip:port" string to NodeId.
            NodeId discovered_peer_node = address_to_node_id(sender_id_str);

            if (discovered_peer_node.id.empty()) {
                // address_to_node_id returns an empty/invalid NodeId on parsing failure.
                LOG_ERROR("handle_discovery_message: Failed to parse NodeId from PEER_DISCOVERY message sender: " << sender_id_str);
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
            nlohmann::json discovery_data;
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
                    // LOG_INFO("Sent PEER_DISCOVERY back to " << discovered_peer_node.id << " after UDP discovery.");
                } catch (const zmq::error_t& e) {
                    // Log if sending the reciprocal discovery message fails. This is not critical but good for diagnostics.
                    LOG_ERROR("Failed to send PEER_DISCOVERY back to " << discovered_peer_node.id
                              << " after UDP discovery: " << e.what());
                }
            } else {
                // This might happen if connect_to_peer failed or is asynchronous and not yet complete.
                // LOG_ERROR("Could not send PEER_DISCOVERY back to " << discovered_peer_node.id
                //           << " after UDP discovery: Peer not found or not connected.");
            }
            
        } else {
            // Optional: Log if other unexpected message types are received on the discovery port.
            // LOG_ERROR("handle_discovery_message: Received non-PEER_DISCOVERY message on discovery socket. Type: "
            //           << static_cast<int>(type));
        }
    } catch (const nlohmann::json::exception& e) { // Catch nlohmann::json specific exceptions
        LOG_ERROR("handle_discovery_message: JSON parsing/access error: " << e.what() << " Message: " << msg_str.substr(0,100));
    } catch (const zmq::error_t& e) {
        // Avoid error spam if socket is closed during shutdown or resource temporarily unavailable.
        if (running_.load() && e.num() != ETERM && e.num() != EAGAIN) {
             LOG_ERROR("handle_discovery_message: ZMQ error: " << e.what());
        }
    } catch (const std::exception& e) {
         // Avoid error spam on shutdown.
         if (running_.load()) {
            LOG_ERROR("handle_discovery_message: Standard error: " << e.what());
        }
    }
}

void MeshNetwork::heartbeat_loop() {
    while (running_.load()) {
        // Send heartbeat to all connected peers
        nlohmann::json heartbeat_data;
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
                        LOG_ERROR("Failed to send heartbeat to " << pair.first.id
                                 << ": " << e.what());
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
            LOG_INFO("Peer " << peer_id.id << " timed out, disconnecting");
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
        
        LOG_INFO("Connected to peer: " << peer_id.id);
        
    } catch (const zmq::error_t& e) {
        LOG_ERROR("Failed to connect to peer " << peer_id.id << ": " << e.what());
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
        // LOG_INFO("Disconnected from peer: " << peer_id.id); // Logging will be updated later
        LOG_INFO("Disconnected from peer: " << peer_id.id);
        peers_.erase(it); // Remove peer from map
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
    nlohmann::json discovery_data;
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
                LOG_ERROR("Failed to send discovery request to " << pair.first.id
                         << ": " << e.what());
            }
        }
    }
}

void MeshNetwork::send_peer_list(const NodeId& requester) {
    nlohmann::json peer_list_data;
    peer_list_data["sender"] = local_node_.id;
    
    nlohmann::json peers_array = nlohmann::json::array(); // Create a JSON array
    
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto& pair : peers_) {
            if (pair.second->is_connected) {
                peers_array.push_back(pair.first.id); // Add elements to JSON array
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
            LOG_ERROR("Failed to send peer list to " << requester.id
                     << ": " << e.what());
        }
    }
}

void MeshNetwork::broadcast_discovery() {
    // Entire body commented out due to ZMQ_DGRAM issue
    /*
    // Use UDP broadcast for initial peer discovery
    nlohmann::json discovery_data;
    discovery_data["sender"] = local_node_.id;
    discovery_data["port"] = local_node_.port;
    discovery_data["discovery_port"] = discovery_port_;
    discovery_data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::string discovery_msg = create_message(MessageType::PEER_DISCOVERY, discovery_data);
    
    try {
        // Broadcast to local network using multicast.
        // Using multicast address 239.255.0.1 for local network discovery.
        std::string broadcast_address = "udp://239.255.0.1:" + std::to_string(discovery_port_);
        
        // ZMQ_DGRAM is defined as 18 in zmq.h, but having issues with visibility via zmq.hpp
        zmq::socket_t broadcast_socket(context_, zmq::socket_type::dgram);
        broadcast_socket.connect(broadcast_address);
        
        zmq::message_t msg(discovery_msg.size());
        memcpy(msg.data(), discovery_msg.c_str(), discovery_msg.size());
        broadcast_socket.send(msg, zmq::send_flags::dontwait);
        
        broadcast_socket.close();
        
    } catch (const zmq::error_t& e) {
        // Broadcast might fail in some network configurations - that's okay
        // We rely on seed nodes for bootstrap
    }
    */
    LOG_INFO("broadcast_discovery temporarily disabled due to ZeroMQ DGRAM issue.");
}

// The definition for parse_message(const std::string& message, MessageType& type, std::string& out_errors)
// has been moved to mesh_network.h as an inline function.
// This redundant definition in mesh_network.cpp is now removed.

// Large commented-out handle_message block removed.

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

// --- Definitions moved from mesh_network.h ---

// Forward declare or ensure all process_X methods are above handle_message if not already
nlohmann::json MeshNetwork::parse_message(const std::string& message, MessageType& type, std::string& out_errors) {
    try {
        nlohmann::json root = nlohmann::json::parse(message);
        if (root.contains("type") && root["type"].is_number_unsigned() &&
            root.contains("data") && root["data"].is_object()) {
            type = static_cast<MessageType>(root["type"].get<unsigned int>());
            return root["data"];
        } else {
            out_errors = "Message missing 'type' or 'data', or they have incorrect format.";
        }
    } catch (const nlohmann::json::parse_error& e) {
        out_errors = std::string("JSON parsing exception: ") + e.what();
    } catch (const std::exception& e) {
        out_errors = std::string("Generic parsing exception: ") + e.what();
    }
    return nlohmann::json(); // Return a null JSON object on error
}

void MeshNetwork::handle_message(const zmq::message_t& identity, const zmq::message_t& message) {
    try {
        std::string msg_str(static_cast<const char*>(message.data()), message.size());
        MessageType type;
        std::string parse_err_str;
        nlohmann::json data = parse_message(msg_str, type, parse_err_str);

        if (data.is_null()) { // Check for null JSON object
            LOG_ERROR("handle_message: Failed to parse router message. Error: " << parse_err_str
                      << ". Raw message (approx first 100 chars): " << msg_str.substr(0, 100));
            return;
        }

        if (!data.contains("sender") || !data["sender"].is_string()) {
            LOG_ERROR("handle_message: Message (type " << static_cast<int>(type)
                      << ") missing or invalid 'sender' field. Data: " << data.dump(4));
            return;
        }
        NodeId sender = address_to_node_id(data["sender"].get<std::string>());
        if (sender.id.empty()) {
            LOG_ERROR("handle_message: Failed to parse sender NodeId from message. Sender string: " << data["sender"].get<std::string>());
            return;
        }
        update_peer_last_seen(sender);

        switch (type) {
            case MessageType::HEARTBEAT: process_heartbeat(sender, data); break;
            case MessageType::PEER_DISCOVERY: process_peer_discovery(sender, data); break;
            case MessageType::UNICAST: process_unicast(sender, data); break;
            case MessageType::BROADCAST: process_broadcast(sender, data); break;
            case MessageType::PEER_LIST: process_peer_list(sender, data); break;
            case MessageType::GOODBYE: process_goodbye(sender, data); break;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error handling message: " << e.what());
    }
}


MeshNetwork::MeshNetwork(const std::string& local_ip, uint16_t local_port,
                        uint16_t discovery_port)
    : context_(1), local_node_(local_ip, local_port), running_(false),
      discovery_port_(discovery_port ? discovery_port : local_port + 1000),
      rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {
    LOG_INFO("MeshNetwork constructor for " << local_node_.id);
    // Sockets are initialized here, ensuring they are new.
    router_socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::router);
    // discovery_socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::dgram); // Commented out for compilation
}

MeshNetwork::~MeshNetwork() {
    LOG_INFO("MeshNetwork destructor for " << local_node_.id);
    stop();
}

bool MeshNetwork::start() {
    LOG_INFO("MeshNetwork::start() called for " << local_node_.id);
    if (running_.load()) {
        LOG_INFO("MeshNetwork::start(): Already running for " << local_node_.id);
        return false;
    }

    LOG_INFO("MeshNetwork::start(): Initializing sockets for " << local_node_.id);
    // Reset and re-create sockets to ensure they are fresh
    // This is crucial if start() can be called after stop() on the same object.
    if (router_socket_) {
        LOG_INFO("MeshNetwork::start(): Resetting existing router_socket_ for " << local_node_.id);
        // It's good practice to ensure the socket is closed before reset if it might be open
        try { router_socket_->close(); } catch (const zmq::error_t&) { /* ignore, may not be open/valid */ }
        router_socket_.reset();
    }
    router_socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::router);
    LOG_INFO("MeshNetwork::start(): New router_socket_ created for " << local_node_.id);

    if (discovery_socket_) {
        LOG_INFO("MeshNetwork::start(): Resetting existing discovery_socket_ for " << local_node_.id);
        try { discovery_socket_->close(); } catch (const zmq::error_t&) { /* ignore */ }
        discovery_socket_.reset();
    }
    // discovery_socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::dgram); // Commented out for compilation
    // LOG_INFO("MeshNetwork::start(): New discovery_socket_ created for " << local_node_.id); // Commented out as it relates to dgram

    try {
        LOG_INFO("MeshNetwork::start(): Binding sockets for " << local_node_.id);
        std::string router_address = "tcp://*:" + std::to_string(local_node_.port);
        router_socket_->bind(router_address);

        // std::string discovery_address = "udp://*:" + std::to_string(discovery_port_); // Commented out
        // if (discovery_socket_) discovery_socket_->bind(discovery_address); // Commented out

        int linger = 0;
        router_socket_->set(zmq::sockopt::linger, linger);
        // if (discovery_socket_) discovery_socket_->set(zmq::sockopt::linger, linger); // Commented out

        LOG_INFO("MeshNetwork::start(): Setting socket options for " << local_node_.id);
        // Redundant linger declaration removed.

        LOG_INFO("MeshNetwork::start(): Starting threads for " << local_node_.id);
        running_.store(true);
        message_handler_thread_ = std::thread(&MeshNetwork::message_handler_loop, this);
        LOG_INFO("MeshNetwork::start(): message_handler_thread_ created for " << local_node_.id);
        heartbeat_thread_ = std::thread(&MeshNetwork::heartbeat_loop, this);
        LOG_INFO("MeshNetwork::start(): heartbeat_thread_ created for " << local_node_.id);
        discovery_thread_ = std::thread(&MeshNetwork::discovery_loop, this);
        LOG_INFO("MeshNetwork::start(): discovery_thread_ created for " << local_node_.id);
        cleanup_thread_ = std::thread(&MeshNetwork::cleanup_loop, this);
        LOG_INFO("MeshNetwork::start(): cleanup_thread_ created for " << local_node_.id);

        LOG_INFO("MeshNetwork started successfully on " << local_node_.id);
        return true;

    } catch (const zmq::error_t& e) {
        LOG_ERROR("Failed to start MeshNetwork for " << local_node_.id << ": " << e.what());
        running_.store(false); // Ensure running is false if start fails
        return false;
    }
}

void MeshNetwork::stop() {
    LOG_INFO("MeshNetwork::stop() called for " << local_node_.id);
    if (!running_.load()) {
        LOG_INFO("MeshNetwork::stop(): Already stopped for " << local_node_.id);
        return;
    }

    LOG_INFO("MeshNetwork::stop(): Sending GOODBYE from " << local_node_.id);
    nlohmann::json goodbye_data;
    goodbye_data["sender"] = local_node_.id; // Changed "node_id" to "sender"
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

    std::cout << "MeshNetwork::stop(): Setting running_ to false for " << local_node_.id << std::endl;
    running_.store(false); // Signal threads to terminate

    // Note: Closing sockets before joining threads can help unblock threads stuck in socket operations.
    // However, the current approach is: signal stop, join threads, then clean up sockets.
    // For ZMQ, poll timeouts in thread loops are crucial for responsiveness to `running_ == false`.

    if (message_handler_thread_.joinable()) {
        LOG_INFO("MeshNetwork::stop(): Joining message_handler_thread_ for " << local_node_.id);
        message_handler_thread_.join();
        LOG_INFO("MeshNetwork::stop(): message_handler_thread_ joined for " << local_node_.id);
    }
    if (heartbeat_thread_.joinable()) {
        LOG_INFO("MeshNetwork::stop(): Joining heartbeat_thread_ for " << local_node_.id);
        heartbeat_thread_.join();
        LOG_INFO("MeshNetwork::stop(): heartbeat_thread_ joined for " << local_node_.id);
    }
    if (discovery_thread_.joinable()) {
        LOG_INFO("MeshNetwork::stop(): Joining discovery_thread_ for " << local_node_.id);
        discovery_thread_.join();
        LOG_INFO("MeshNetwork::stop(): discovery_thread_ joined for " << local_node_.id);
    }
    if (cleanup_thread_.joinable()) {
        LOG_INFO("MeshNetwork::stop(): Joining cleanup_thread_ for " << local_node_.id);
        cleanup_thread_.join();
        LOG_INFO("MeshNetwork::stop(): cleanup_thread_ joined for " << local_node_.id);
    }

    LOG_INFO("MeshNetwork::stop(): All threads joined for " << local_node_.id);
    LOG_INFO("MeshNetwork::stop(): Closing and resetting sockets for " << local_node_.id);
    if (router_socket_) {
        try {
            // router_socket_->setsockopt(ZMQ_LINGER, 0); // Linger is set at socket creation in start()
            router_socket_->close();
        } catch (const zmq::error_t& e) {
            LOG_ERROR("MeshNetwork::stop(): Error closing router_socket for " << local_node_.id << ": " << e.what());
        }
        router_socket_.reset();
        LOG_INFO("MeshNetwork::stop(): router_socket_ reset for " << local_node_.id);
    }
    if (discovery_socket_) {
        try {
            // discovery_socket_->setsockopt(ZMQ_LINGER, 0); // Linger is set at socket creation in start()
            discovery_socket_->close();
        } catch (const zmq::error_t& e) {
            LOG_ERROR("MeshNetwork::stop(): Error closing discovery_socket for " << local_node_.id << ": " << e.what());
        }
        discovery_socket_.reset();
        LOG_INFO("MeshNetwork::stop(): discovery_socket_ reset for " << local_node_.id);
    }

    LOG_INFO("MeshNetwork::stop(): Clearing peers for " << local_node_.id);
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        peers_.clear(); // This will call destructors of PeerInfo, which should reset their unique_ptr sockets
    }

    LOG_INFO("MeshNetwork stopped for " << local_node_.id);
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

    nlohmann::json data_json; // Renamed to avoid conflict with 'data' parameter in other functions
    data_json["sender"] = local_node_.id;
    data_json["target"] = target.id;
    data_json["message"] = message;
    data_json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::string msg_payload = create_message(MessageType::UNICAST, data_json);

    try {
        zmq::message_t zmq_msg(msg_payload.size());
        memcpy(zmq_msg.data(), msg_payload.c_str(), msg_payload.size());
        it->second->socket->send(zmq_msg, zmq::send_flags::dontwait);
        return true;
    } catch (const zmq::error_t& e) {
        LOG_ERROR("Failed to send unicast to " << target.id << ": " << e.what());
        return false;
    }
}

bool MeshNetwork::send_broadcast(const std::string& message) {
    nlohmann::json data_json; // Renamed
    data_json["sender"] = local_node_.id;
    data_json["message"] = message;
    data_json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::string msg_payload = create_message(MessageType::BROADCAST, data_json);

    std::lock_guard<std::mutex> lock(peers_mutex_);
    bool success = false;

    for (auto& pair : peers_) {
        if (pair.second->is_connected && pair.second->socket) {
            try {
                zmq::message_t zmq_msg(msg_payload.size());
                memcpy(zmq_msg.data(), msg_payload.c_str(), msg_payload.size());
                pair.second->socket->send(zmq_msg, zmq::send_flags::dontwait);
                success = true;
            } catch (const zmq::error_t& e) {
                LOG_ERROR("Failed to send broadcast to " << pair.first.id
                         << ": " << e.what());
            }
        }
    }

    return success;
}

std::vector<NodeId> MeshNetwork::get_connected_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::vector<NodeId> connected_peers_list; // Renamed

    for (const auto& pair : peers_) {
        if (pair.second->is_connected) {
            connected_peers_list.push_back(pair.first);
        }
    }

    return connected_peers_list;
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
        { *router_socket_, 0, ZMQ_POLLIN, 0 }      // Use * to get the socket handle
        // { *discovery_socket_, 0, ZMQ_POLLIN, 0 }  // Commented out
    };

    while (running_.load()) {
        try {
            zmq::poll(items, 1, std::chrono::milliseconds(100)); // Poll only router_socket_

            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t identity, received_message;

                if (router_socket_->recv(identity, zmq::recv_flags::dontwait) &&
                    router_socket_->recv(received_message, zmq::recv_flags::dontwait)) {
                    handle_message(identity, received_message);
                }
            }
            // if (items[1].revents & ZMQ_POLLIN) { // Commented out discovery socket handling
            //      zmq::message_t discovery_msg_zmq;
            //      // Assuming discovery_socket_ is a regular socket, not REQ/REP, so just recv
            //      if(discovery_socket_ && discovery_socket_->recv(discovery_msg_zmq, zmq::recv_flags::dontwait)) { // Added check for discovery_socket_
            //         handle_discovery_message(discovery_msg_zmq);
            //      }
            // }
        } catch (const zmq::error_t& e) {
            if (running_.load() && e.num() != ETERM) {
                LOG_ERROR("Message handler_loop ZMQ error: " << e.what());
            }
        } catch (const std::exception& e) {
            if(running_.load()) {
                 LOG_ERROR("Message handler_loop std error: " << e.what());
            }
        }
    }
}

std::string MeshNetwork::create_message(MessageType type, const nlohmann::json& data) {
    nlohmann::json message_root;
    message_root["type"] = static_cast<uint8_t>(type);
    message_root["data"] = data;
    return message_root.dump();
}