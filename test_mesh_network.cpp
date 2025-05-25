// test_mesh_network.cpp - Comprehensive test suite for mesh network

#include "mesh_network.h"
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <algorithm>
#include <cassert>

class TestNode {
private:
    std::unique_ptr<MeshNetwork> network_;
    std::string node_id_;
    std::vector<std::string> received_unicasts_;
    std::vector<std::string> received_broadcasts_;
    std::mutex message_mutex_;
    std::atomic<int> unicast_count_;
    std::atomic<int> broadcast_count_;
    
public:
    TestNode(const std::string& ip, uint16_t port) 
        : node_id_(ip + ":" + std::to_string(port)), unicast_count_(0), broadcast_count_(0) {
        network_ = std::make_unique<MeshNetwork>(ip, port);
        
        network_->set_unicast_callback([this](const NodeId& sender, const std::string& message) {
            std::lock_guard<std::mutex> lock(message_mutex_);
            received_unicasts_.push_back(sender.id + ": " + message);
            unicast_count_.fetch_add(1);
        });
        
        network_->set_broadcast_callback([this](const NodeId& sender, const std::string& message) {
            std::lock_guard<std::mutex> lock(message_mutex_);
            received_broadcasts_.push_back(sender.id + ": " + message);
            broadcast_count_.fetch_add(1);
        });
    }
    
    bool start() { return network_->start(); }
    void stop() { network_->stop(); }
    
    void add_seed_node(const std::string& ip, uint16_t port) {
        network_->add_seed_node(ip, port);
    }
    
    bool send_unicast(const NodeId& target, const std::string& message) {
        return network_->send_unicast(target, message);
    }
    
    bool send_broadcast(const std::string& message) {
        return network_->send_broadcast(message);
    }
    
    std::vector<NodeId> get_connected_peers() const {
        return network_->get_connected_peers();
    }
    
    size_t get_peer_count() const {
        return network_->get_peer_count();
    }
    
    int get_unicast_count() const { return unicast_count_.load(); }
    int get_broadcast_count() const { return broadcast_count_.load(); }
    
    std::vector<std::string> get_received_unicasts() const {
        std::lock_guard<std::mutex> lock(message_mutex_);
        return received_unicasts_;
    }
    
    std::vector<std::string> get_received_broadcasts() const {
        std::lock_guard<std::mutex> lock(message_mutex_);
        return received_broadcasts_;
    }
    
    const std::string& get_node_id() const { return node_id_; }
    
    void clear_messages() {
        std::lock_guard<std::mutex> lock(message_mutex_);
        received_unicasts_.clear();
        received_broadcasts_.clear();
        unicast_count_.store(0);
        broadcast_count_.store(0);
    }
};

class MeshNetworkTester {
private:
    std::vector<std::unique_ptr<TestNode>> nodes_;
    std::mt19937 rng_;
    
public:
    MeshNetworkTester() : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}
    
    void create_test_network(int num_nodes, const std::string& base_ip = "127.0.0.1", 
                           uint16_t base_port = 9000) {
        std::cout << "Creating test network with " << num_nodes << " nodes..." << std::endl;
        
        // Create nodes
        for (int i = 0; i < num_nodes; i++) {
            auto node = std::make_unique<TestNode>(base_ip, base_port + i);
            nodes_.push_back(std::move(node));
        }
        
        // Set up seed connections (each node knows about the next one)
        for (size_t i = 0; i < nodes_.size(); i++) {
            size_t next = (i + 1) % nodes_.size();
            nodes_[i]->add_seed_node(base_ip, base_port + next);
            
            // Also add one random seed for better connectivity
            if (nodes_.size() > 2) {
                size_t random_idx;
                do {
                    random_idx = rng_() % nodes_.size();
                } while (random_idx == i);
                nodes_[i]->add_seed_node(base_ip, base_port + random_idx);
            }
        }
        
        std::cout << "Test network created." << std::endl;
    }
    
    bool start_all_nodes() {
        std::cout << "Starting all nodes..." << std::endl;
        bool success = true;
        
        for (auto& node : nodes_) {
            if (!node->start()) {
                std::cerr << "Failed to start node " << node->get_node_id() << std::endl;
                success = false;
            }
        }
        
        if (success) {
            std::cout << "All nodes started successfully." << std::endl;
        }
        
        return success;
    }
    
    void stop_all_nodes() {
        std::cout << "Stopping all nodes..." << std::endl;
        for (auto& node : nodes_) {
            node->stop();
        }
        std::cout << "All nodes stopped." << std::endl;
    }
    
    void wait_for_convergence(int timeout_seconds = 30) {
        std::cout << "Waiting for network convergence..." << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        bool converged = false;
        
        while (!converged && 
               std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - start_time).count() < timeout_seconds) {
            
            converged = true;
            size_t expected_peers = nodes_.size() - 1; // Each node should see all others
            
            for (const auto& node : nodes_) {
                if (node->get_peer_count() < expected_peers) {
                    converged = false;
                    break;
                }
            }
            
            if (!converged) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        
        if (converged) {
            std::cout << "Network converged successfully." << std::endl;
        } else {
            std::cout << "Network convergence timeout!" << std::endl;
        }
        
        // Print connectivity status
        for (const auto& node : nodes_) {
            std::cout << "Node " << node->get_node_id() << " connected to " 
                     << node->get_peer_count() << " peers" << std::endl;
        }
    }
    
    bool test_unicast_messaging() {
        std::cout << "\n=== Testing Unicast Messaging ===" << std::endl;
        
        if (nodes_.size() < 2) {
            std::cout << "Need at least 2 nodes for unicast test" << std::endl;
            return false;
        }
        
        // Clear previous messages
        for (auto& node : nodes_) {
            node->clear_messages();
        }
        
        bool success = true;
        int test_messages = 5;
        
        // Test unicast from first node to all others
        for (size_t target_idx = 1; target_idx < nodes_.size(); target_idx++) {
            auto target_peers = nodes_[target_idx]->get_connected_peers();
            NodeId target_node;
            
            // Find the target node ID
            for (const auto& peer : target_peers) {
                if (peer.id.find("127.0.0.1:900") != std::string::npos) {
                    // This is a bit hacky - in practice you'd have a better way to identify nodes
                    size_t port_pos = peer.id.find_last_of(':');
                    if (port_pos != std::string::npos) {
                        uint16_t port = std::stoi(peer.id.substr(port_pos + 1));
                        if (port == 9000 + target_idx) {
                            target_node = peer;
                            break;
                        }
                    }
                }
            }
            
            // Use direct node ID construction instead
            target_node = NodeId("127.0.0.1", 9000 + target_idx);
            
            for (int i = 0; i < test_messages; i++) {
                std::string message = "Unicast test message " + std::to_string(i) + 
                                    " to node " + std::to_string(target_idx);
                
                if (!nodes_[0]->send_unicast(target_node, message)) {
                    std::cout << "Failed to send unicast message " << i << " to node " 
                             << target_idx << std::endl;
                    success = false;
                }
            }
        }
        
        // Wait for message delivery
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Verify message reception
        for (size_t target_idx = 1; target_idx < nodes_.size(); target_idx++) {
            int received = nodes_[target_idx]->get_unicast_count();
            if (received != test_messages) {
                std::cout << "Node " << target_idx << " received " << received 
                         << " unicast messages, expected " << test_messages << std::endl;
                success = false;
            } else {
                std::cout << "Node " << target_idx << " correctly received " 
                         << received << " unicast messages" << std::endl;
            }
        }
        
        if (success) {
            std::cout << "Unicast messaging test PASSED" << std::endl;
        } else {
            std::cout << "Unicast messaging test FAILED" << std::endl;
        }
        
        return success;
    }
    
    bool test_broadcast_messaging() {
        std::cout << "\n=== Testing Broadcast Messaging ===" << std::endl;
        
        if (nodes_.size() < 2) {
            std::cout << "Need at least 2 nodes for broadcast test" << std::endl;
            return false;
        }
        
        // Clear previous messages
        for (auto& node : nodes_) {
            node->clear_messages();
        }
        
        bool success = true;
        int test_messages = 3;
        
        // Send broadcast messages from first node
        for (int i = 0; i < test_messages; i++) {
            std::string message = "Broadcast test message " + std::to_string(i);
            
            if (!nodes_[0]->send_broadcast(message)) {
                std::cout << "Failed to send broadcast message " << i << std::endl;
                success = false;
            }
        }
        
        // Wait for message delivery
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Verify all other nodes received the broadcasts
        for (size_t node_idx = 1; node_idx < nodes_.size(); node_idx++) {
            int received = nodes_[node_idx]->get_broadcast_count();
            if (received != test_messages) {
                std::cout << "Node " << node_idx << " received " << received 
                         << " broadcast messages, expected " << test_messages << std::endl;
                success = false;
            } else {
                std::cout << "Node " << node_idx << " correctly received " 
                         << received << " broadcast messages" << std::endl;
            }
        }
        
        if (success) {
            std::cout << "Broadcast messaging test PASSED" << std::endl;
        } else {
            std::cout << "Broadcast messaging test FAILED" << std::endl;
        }
        
        return success;
    }
    
    bool test_node_failure_recovery() {
        std::cout << "\n=== Testing Node Failure and Recovery ===" << std::endl;
        
        if (nodes_.size() < 3) {
            std::cout << "Need at least 3 nodes for failure recovery test" << std::endl;
            return false;
        }
        
        bool success = true;
        
        // Record initial connectivity
        size_t initial_peer_count = nodes_[0]->get_peer_count();
        std::cout << "Initial peer count for node 0: " << initial_peer_count << std::endl;
        
        // Stop middle node
        size_t failed_node_idx = nodes_.size() / 2;
        std::cout << "Stopping node " << failed_node_idx << "..." << std::endl;
        nodes_[failed_node_idx]->stop();
        
        // Wait for failure detection
        std::this_thread::sleep_for(std::chrono::seconds(20));
        
        // Check that other nodes detected the failure
        size_t peer_count_after_failure = nodes_[0]->get_peer_count();
        std::cout << "Peer count after failure: " << peer_count_after_failure << std::endl;
        
        if (peer_count_after_failure >= initial_peer_count) {
            std::cout << "Node failure not detected properly" << std::endl;
            success = false;
        }
        
        // Restart the failed node
        std::cout << "Restarting node " << failed_node_idx << "..." << std::endl;
        if (!nodes_[failed_node_idx]->start()) {
            std::cout << "Failed to restart node " << failed_node_idx << std::endl;
            return false;
        }
        
        // Wait for recovery
        std::this_thread::sleep_for(std::chrono::seconds(15));
        
        // Check recovery
        size_t peer_count_after_recovery = nodes_[0]->get_peer_count();
        std::cout << "Peer count after recovery: " << peer_count_after_recovery << std::endl;
        
        if (peer_count_after_recovery < initial_peer_count) {
            std::cout << "Node recovery incomplete" << std::endl;
            success = false;
        }
        
        if (success) {
            std::cout << "Node failure and recovery test PASSED" << std::endl;
        } else {
            std::cout << "Node failure and recovery test FAILED" << std::endl;
        }
        
        return success;
    }
    
    bool test_stress_messaging() {
        std::cout << "\n=== Testing Stress Messaging ===" << std::endl;
        
        if (nodes_.size() < 2) {
            std::cout << "Need at least 2 nodes for stress test" << std::endl;
            return false;
        }
        
        // Clear previous messages
        for (auto& node : nodes_) {
            node->clear_messages();
        }
        
        bool success = true;
        int messages_per_node = 50;
        int total_expected_broadcasts = messages_per_node * nodes_.size();
        
        std::cout << "Sending " << messages_per_node << " messages from each node..." << std::endl;
        
        // All nodes send broadcasts simultaneously
        std::vector<std::thread> sender_threads;
        
        for (size_t node_idx = 0; node_idx < nodes_.size(); node_idx++) {
            sender_threads.emplace_back([this, node_idx, messages_per_node]() {
                for (int i = 0; i < messages_per_node; i++) {
                    std::string message = "Stress test from node " + 
                                        std::to_string(node_idx) + " message " + std::to_string(i);
                    nodes_[node_idx]->send_broadcast(message);
                    
                    // Small delay to prevent overwhelming
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });
        }
        
        // Wait for all senders to complete
        for (auto& thread : sender_threads) {
            thread.join();
        }
        
        // Wait for message delivery
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Verify message reception
        for (size_t node_idx = 0; node_idx < nodes_.size(); node_idx++) {
            int received = nodes_[node_idx]->get_broadcast_count();
            int expected = total_expected_broadcasts - messages_per_node; // Don't count own messages
            
            std::cout << "Node " << node_idx << " received " << received 
                     << " messages (expected ~" << expected << ")" << std::endl;
            
            // Allow some tolerance for message loss under stress
            if (received < expected * 0.9) {
                std::cout << "Node " << node_idx << " received too few messages" << std::endl;
                success = false;
            }
        }
        
        if (success) {
            std::cout << "Stress messaging test PASSED" << std::endl;
        } else {
            std::cout << "Stress messaging test FAILED" << std::endl;
        }
        
        return success;
    }
    
    void run_all_tests() {
        std::cout << "Starting comprehensive mesh network test suite..." << std::endl;
        
        create_test_network(5); // Create 5-node test network
        
        if (!start_all_nodes()) {
            std::cout << "Failed to start test network" << std::endl;
            return;
        }
        
        wait_for_convergence();
        
        int passed = 0, total = 0;
        
        // Run all tests
        if (test_unicast_messaging()) passed++; total++;
        if (test_broadcast_messaging()) passed++; total++;
        if (test_node_failure_recovery()) passed++; total++;
        if (test_stress_messaging()) passed++; total++;
        
        stop_all_nodes();
        
        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Passed: " << passed << "/" << total << " tests" << std::endl;
        
        if (passed == total) {
            std::cout << "ALL TESTS PASSED!" << std::endl;
        } else {
            std::cout << "Some tests failed. Check output above for details." << std::endl;
        }
    }
};

int main() {
    try {
        MeshNetworkTester tester;
        tester.run_all_tests();
    } catch (const std::exception& e) {
        std::cerr << "Test suite error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}