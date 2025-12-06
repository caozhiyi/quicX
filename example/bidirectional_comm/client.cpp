#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

class BidirectionalClient {
private:
    std::shared_ptr<quicx::IClient> client_;
    std::string server_url_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{true};
    std::thread heartbeat_thread_;
    std::thread receive_thread_;

    std::queue<std::string> message_queue_;
    std::mutex queue_mutex_;

public:
    BidirectionalClient(const std::string& server_url):
        server_url_(server_url) {
        client_ = quicx::IClient::Create();
    }

    ~BidirectionalClient() { Shutdown(); }

    bool Init() {
        quicx::Http3Config config;
        config.thread_num_ = 2;
        config.log_level_ = quicx::LogLevel::kDebug;
        config.connection_timeout_ms_ = 10000;

        if (!client_->Init(config)) {
            std::cerr << "Failed to initialize client" << std::endl;
            return false;
        }

        return true;
    }

    bool Connect() {
        std::cout << "Connecting to: " << server_url_ << std::endl;

        // Test connection
        auto request = quicx::IRequest::Create();
        std::atomic<bool> test_complete{false};
        bool test_success = false;

        client_->DoRequest(server_url_ + "/connect", quicx::HttpMethod::kGet, request,
            [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                test_success = (error == 0 && response->GetStatusCode() == 200);
                test_complete = true;
            });

        // Wait for test
        for (int i = 0; i < 50 && !test_complete; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (test_success) {
            connected_ = true;
            std::cout << "Connected successfully!" << std::endl;

            // Start heartbeat
            heartbeat_thread_ = std::thread([this]() { HeartbeatLoop(); });

            return true;
        }

        std::cerr << "Connection failed" << std::endl;
        return false;
    }

    void SendMessage(const std::string& message) {
        if (!connected_) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push(message);
            std::cout << "Message queued (not connected)" << std::endl;
            return;
        }

        auto request = quicx::IRequest::Create();
        request->AppendBody(message);

        client_->DoRequest(server_url_ + "/message", quicx::HttpMethod::kPost, request,
            [message](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error != 0) {
                    std::cout << "Failed to send: " << message << std::endl;
                } else {
                    std::cout << "[Server]: " << response->GetBodyAsString() << std::endl;
                }
            });
    }

    void InteractiveMode() {
        std::cout << "\nEnter messages (type 'quit' to exit):" << std::endl;
        std::cout << "> " << std::flush;

        std::string line;
        while (running_ && std::getline(std::cin, line)) {
            if (line == "quit" || line == "exit") {
                break;
            }

            if (!line.empty()) {
                SendMessage(line);
            }

            std::cout << "> " << std::flush;
        }
    }

    void Shutdown() {
        std::cout << "\nShutting down..." << std::endl;

        running_ = false;
        connected_ = false;

        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }

        // Gracefully close all connections (sends CONNECTION_CLOSE frame)
        if (client_) {
            std::cout << "Closing connections gracefully..." << std::endl;
            client_->Close();
        }

        std::cout << "Disconnected" << std::endl;
    }

private:
    void HeartbeatLoop() {
        std::cout << "Heartbeat started (interval: 5s)" << std::endl;

        while (running_ && connected_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            if (!running_) break;

            // Send heartbeat
            auto request = quicx::IRequest::Create();

            client_->DoRequest(server_url_ + "/heartbeat", quicx::HttpMethod::kGet, request,
                [this](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                    if (error != 0) {
                        std::cout << "\nHeartbeat failed! Connection lost." << std::endl;
                        connected_ = false;

                        // Try to reconnect
                        std::cout << "Attempting to reconnect..." << std::endl;
                        if (Connect()) {
                            // Send queued messages
                            FlushMessageQueue();
                        }
                    }
                });
        }
    }

    void FlushMessageQueue() {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        while (!message_queue_.empty()) {
            std::string msg = message_queue_.front();
            message_queue_.pop();
            SendMessage(msg);
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <server_url>" << std::endl;
        std::cout << "Example: " << argv[0] << " https://localhost:8443" << std::endl;
        return 1;
    }

    std::string server_url = argv[1];

    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Bidirectional Communication Client   ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

    BidirectionalClient client(server_url);

    if (!client.Init()) {
        return 1;
    }

    if (!client.Connect()) {
        return 1;
    }

    client.InteractiveMode();
    client.Shutdown();

    return 0;
}
