// example_app.cpp - Example application demonstrating mesh network usage

#include "mesh_network.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <signal.h>

class MeshApplication {
private:
    std::unique_ptr<MeshNetwork> network_;
    std::string node_name_;
    std::atomic<bool> running_;
    std::thread input_thread_;
    
public:
    MeshApplication(const std::string& name, const std::string& ip, uint16_t port)
        : node_name_(name), running_(true) {
        network_ = std::make_unique<MeshNetwork>(ip, port);
        
        // Set up message callbacks
        network_->set_unicast_callback([this](const NodeId& sender, const std::string& message) {
            this->handle_unicast(sender, message);
        });
        
        network_->set_broadcast_callback([this](const NodeId& sender, const std::string& message) {
            this->handle_broadcast(sender, message);
        });
    }
    
    ~MeshApplication() {
        stop();
    }
    
    bool start() {
        if (!network_->start()) {
            std::cerr << "Failed to start mesh network" << std::endl;
            return false;
        }
        
        // Start input handler thread
        input_thread_ = std::thread(&MeshApplication::input_handler, this);
        
        std::cout << "=== Mesh Application [" << node_name_ << "] Started ===" << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  broadcast <message>     - Send broadcast message" << std::endl;
        std::cout << "  unicast <ip:port> <msg> - Send unicast message" << std::endl;
        std::cout << "  peers                   - List connected peers" << std::endl;
        std::cout << "  quit                    - Exit application" << std::endl;
        std::cout << "==============================================" << std::endl;
        
        return true;
    }
    
    void stop() {
        running_.store(false);
        
        if (input_thread_.joinable()) {
            input_thread_.join();
        }
        
        if (network_) {
            network_->stop();
        }
    }
    
    void add_seed_node(const std::string& ip, uint16_t port) {
        network_->add_seed_node(ip, port);
    }
    
    void wait_for_shutdown() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void handle_unicast(const NodeId& sender, const std::string& message) {
        std::cout << "\n[UNICAST from " << sender.id << "]: " << message << std::endl;
        std::cout << "> " << std::flush;
    }
    
    void handle_broadcast(const NodeId& sender, const std::string& message) {
        std::cout << "\n[BROADCAST from " << sender.id << "]: " << message << std::endl;
        std::cout << "> " << std::flush;
    }
    
    void input_handler() {
        std::string line;
        while (running_.load()) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) {
                break;
            }
            
            if (line.empty()) {
                continue;
            }
            
            std::istringstream iss(line);
            std::string command;
            iss >> command;
            
            if (command == "quit" || command == "exit") {
                running_.store(false);
                break;
            } else if (command == "broadcast") {
                std::string message;
                std::getline(iss, message);
                if (!message.empty()) {
                    message = message.substr(1); // Remove leading space
                    if (network_->send_broadcast(message)) {
                        std::cout << "Broadcast sent: " << message << std::endl;
                    } else {
                        std::cout << "Failed to send broadcast" << std::endl;
                    }
                }
            } else if (command == "unicast") {
                std::string target_str, message;
                iss >> target_str;
                std::getline(iss, message);
                
                if (!target_str.empty() && !message.empty()) {
                    message = message.substr(1); // Remove leading space
                    
                    // Parse target (ip:port format)
                    size_t colon_pos = target_str.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string ip = target_str.substr(0, colon_pos);
                        uint16_t port = static_cast<uint16_t>(std::stoi(target_str.substr(colon_pos + 1)));
                        NodeId target(ip, port);
                        
                        if (network_->send_unicast(target, message)) {
                            std::cout << "Unicast sent to " << target.id << ": " << message << std::endl;
                        } else {
                            std::cout << "Failed to send unicast to " << target.id << std::endl;
                        }
                    } else {
                        std::cout << "Invalid target format. Use ip:port" << std::endl;
                    }
                }
            } else if (command == "peers") {
                auto peers = network_->get_connected_peers();
                std::cout << "Connected peers (" << peers.size() << "):" << std::endl;
                for (const auto& peer : peers) {
                    std::cout << "  - " << peer.id << std::endl;
                }
            } else {
                std::cout << "Unknown command: " << command << std::endl;
            }
        }
    }
};

// Global application instance for signal handling
std::unique_ptr<MeshApplication> g_app;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_app) {
        g_app->stop();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " <node_name> <local_ip> <local_port> [seed_ip:seed_port ...]" << std::endl;
        std::cout << "Example: " << argv[0] << " node1 192.168.1.100 8001 192.168.1.101:8002" << std::endl;
        return 1;
    }
    
    std::string node_name = argv[1];
    std::string local_ip = argv[2];
    uint16_t local_port = static_cast<uint16_t>(std::stoi(argv[3]));
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_app = std::make_unique<MeshApplication>(node_name, local_ip, local_port);
        
        // Add seed nodes from command line arguments
        for (int i = 4; i < argc; i++) {
            std::string seed_str = argv[i];
            size_t colon_pos = seed_str.find(':');
            if (colon_pos != std::string::npos) {
                std::string seed_ip = seed_str.substr(0, colon_pos);
                uint16_t seed_port = static_cast<uint16_t>(std::stoi(seed_str.substr(colon_pos + 1)));
                g_app->add_seed_node(seed_ip, seed_port);
                std::cout << "Added seed node: " << seed_ip << ":" << seed_port << std::endl;
            }
        }
        
        if (!g_app->start()) {
            std::cerr << "Failed to start application" << std::endl;
            return 1;
        }
        
        // Wait for shutdown
        g_app->wait_for_shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Application terminated gracefully" << std::endl;
    return 0;
}