#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

// Simple connection pool for demonstration
class ConnectionPool {
private:
    struct ConnectionInfo {
        std::shared_ptr<quicx::IClient> client;
        std::chrono::steady_clock::time_point last_used;
        bool in_use;
        int request_count;

        ConnectionInfo():
            in_use(false),
            request_count(0) {
            last_used = std::chrono::steady_clock::now();
        }
    };

    std::unordered_map<std::string, std::vector<ConnectionInfo>> pool_;
    std::mutex mutex_;
    int max_connections_per_host_;
    int idle_timeout_ms_;
    std::atomic<bool> shutdown_{false};

public:
    ConnectionPool(int max_per_host = 5, int idle_timeout_ms = 30000):
        max_connections_per_host_(max_per_host),
        idle_timeout_ms_(idle_timeout_ms) {}

    std::shared_ptr<quicx::IClient> GetConnection(const std::string& host) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return nullptr;
        }

        auto& connections = pool_[host];

        // Try to find an available connection
        for (auto& conn_info : connections) {
            if (!conn_info.in_use) {
                // Check if connection is still valid (not idle too long)
                auto now = std::chrono::steady_clock::now();
                auto idle_time =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - conn_info.last_used).count();

                if (idle_time < idle_timeout_ms_) {
                    conn_info.in_use = true;
                    conn_info.last_used = now;
                    conn_info.request_count++;
                    std::cout << "  Reused existing connection (saved handshake time!)" << std::endl;
                    std::cout << "    Total requests on this connection: " << conn_info.request_count << std::endl;
                    return conn_info.client;
                } else {
                    std::cout << "  Connection idle for " << idle_time << "ms, creating new one" << std::endl;
                }
            }
        }

        // Create new connection if under limit
        if (connections.size() < static_cast<size_t>(max_connections_per_host_)) {
            ConnectionInfo new_conn;
            new_conn.client = quicx::IClient::Create();

            quicx::Http3Config config;
            config.thread_num_ = 2;
            config.log_level_ = quicx::LogLevel::kWarn;
            config.connection_timeout_ms_ = 5000;

            if (new_conn.client->Init(config)) {
                new_conn.in_use = true;
                new_conn.request_count = 1;
                connections.push_back(new_conn);
                std::cout << "  Created new connection to " << host << std::endl;
                std::cout << "    Total connections in pool: " << connections.size() << std::endl;
                return new_conn.client;
            }
        }

        std::cout << "  Pool limit reached, waiting for available connection..." << std::endl;
        return nullptr;
    }

    void ReleaseConnection(const std::string& host, std::shared_ptr<quicx::IClient> client) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = pool_.find(host);
        if (it != pool_.end()) {
            for (auto& conn_info : it->second) {
                if (conn_info.client == client) {
                    conn_info.in_use = false;
                    conn_info.last_used = std::chrono::steady_clock::now();
                    return;
                }
            }
        }
    }

    void HealthCheck() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::cout << "\nPerforming health check..." << std::endl;

        for (auto& [host, connections] : pool_) {
            std::cout << "  Host: " << host << std::endl;

            for (size_t i = 0; i < connections.size(); ++i) {
                auto& conn = connections[i];
                auto now = std::chrono::steady_clock::now();
                auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - conn.last_used).count();

                std::cout << "    Connection " << i << ": ";
                std::cout << (conn.in_use ? "IN USE" : "IDLE");
                std::cout << ", idle for " << idle_time << "ms";
                std::cout << ", " << conn.request_count << " requests";

                if (idle_time > idle_timeout_ms_) {
                    std::cout << " [EXPIRED]";
                } else {
                    std::cout << " [HEALTHY]";
                }
                std::cout << std::endl;
            }
        }
    }

    void CleanupIdleConnections() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::cout << "\nCleaning up idle connections..." << std::endl;

        for (auto& [host, connections] : pool_) {
            auto now = std::chrono::steady_clock::now();

            connections.erase(
                std::remove_if(connections.begin(), connections.end(),
                    [&](const ConnectionInfo& conn) {
                        if (conn.in_use) return false;

                        auto idle_time =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - conn.last_used).count();

                        if (idle_time > idle_timeout_ms_) {
                            std::cout << "  Removed idle connection to " << host << std::endl;
                            return true;
                        }
                        return false;
                    }),
                connections.end());
        }
    }

    void GracefulShutdown() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::cout << "\nInitiating graceful shutdown..." << std::endl;
        shutdown_ = true;

        int total_connections = 0;
        for (const auto& [host, connections] : pool_) {
            total_connections += connections.size();
        }

        std::cout << "  Closing " << total_connections << " connections..." << std::endl;
        pool_.clear();
        std::cout << "  All connections closed gracefully ✓" << std::endl;
    }

    void PrintStats() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::cout << "\nConnection Pool Statistics:" << std::endl;
        std::cout << "  Hosts: " << pool_.size() << std::endl;

        int total_connections = 0;
        int total_requests = 0;

        for (const auto& [host, connections] : pool_) {
            total_connections += connections.size();
            for (const auto& conn : connections) {
                total_requests += conn.request_count;
            }
        }

        std::cout << "  Total connections: " << total_connections << std::endl;
        std::cout << "  Total requests: " << total_requests << std::endl;
        if (total_connections > 0) {
            std::cout << "  Avg requests per connection: " << (total_requests / total_connections) << std::endl;
        }
    }
};

class ConnectionLifecycleDemo {
private:
    ConnectionPool pool_;
    std::string base_url_;

public:
    ConnectionLifecycleDemo(const std::string& base_url):
        pool_(5, 30000),
        base_url_(base_url) {}

    void RunDemo() {
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║  Connection Lifecycle Demo            ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

        Test1_ConnectionReuse();
        Test2_HealthCheck();
        Test3_IdleCleanup();
        Test4_GracefulShutdown();

        std::cout << "\n========================================" << std::endl;
        std::cout << "Demo completed!" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }

private:
    void Test1_ConnectionReuse() {
        std::cout << "Test 1: Connection Reuse" << std::endl;
        std::cout << "========================================" << std::endl;

        // Make 5 requests to the same host
        for (int i = 1; i <= 5; ++i) {
            std::cout << "\nRequest " << i << ":" << std::endl;

            auto client = pool_.GetConnection(base_url_);
            if (!client) {
                std::cout << "  Failed to get connection" << std::endl;
                continue;
            }

            MakeRequest(client, base_url_ + "/hello");

            pool_.ReleaseConnection(base_url_, client);

            // Small delay between requests
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        pool_.PrintStats();
    }

    void Test2_HealthCheck() {
        std::cout << "\n\nTest 2: Health Check" << std::endl;
        std::cout << "========================================" << std::endl;

        pool_.HealthCheck();
    }

    void Test3_IdleCleanup() {
        std::cout << "\n\nTest 3: Idle Connection Cleanup" << std::endl;
        std::cout << "========================================" << std::endl;

        std::cout << "Waiting 2 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        pool_.CleanupIdleConnections();
        pool_.PrintStats();
    }

    void Test4_GracefulShutdown() {
        std::cout << "\n\nTest 4: Graceful Shutdown" << std::endl;
        std::cout << "========================================" << std::endl;

        pool_.GracefulShutdown();
    }

    void MakeRequest(std::shared_ptr<quicx::IClient> client, const std::string& url) {
        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        client->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error != 0) {
                    std::cout << "    Request failed: " << error << std::endl;
                } else {
                    std::cout << "    Success: HTTP " << response->GetStatusCode() << std::endl;
                }
                completed = true;
            });

        // Wait for completion
        for (int i = 0; i < 50 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

int main(int argc, char* argv[]) {
    std::string base_url = argc > 1 ? argv[1] : "https://localhost:8443";

    std::cout << "Connection Lifecycle Management Demo" << std::endl;
    std::cout << "Target: " << base_url << std::endl;

    ConnectionLifecycleDemo demo(base_url);
    demo.RunDemo();

    return 0;
}
