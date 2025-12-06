#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

class ErrorHandlingClient {
private:
    std::shared_ptr<quicx::IClient> client_;
    std::string base_url_;

public:
    ErrorHandlingClient(const std::string& base_url):
        base_url_(base_url) {
        client_ = quicx::IClient::Create();
    }

    bool Init() {
        quicx::Http3Config config;
        config.thread_num_ = 2;
        config.log_level_ = quicx::LogLevel::kWarn;
        config.connection_timeout_ms_ = 5000;  // 5s connection timeout

        if (!client_->Init(config)) {
            std::cerr << "Failed to initialize client" << std::endl;
            return false;
        }

        return true;
    }

    // Test 1: Connection timeout handling
    void TestConnectionTimeout() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test 1: Connection Timeout" << std::endl;
        std::cout << "========================================" << std::endl;

        std::string url = base_url_ + "/timeout";
        std::cout << "Requesting: " << url << std::endl;
        std::cout << "Timeout set to: 5000ms" << std::endl;
        std::cout << "Server will delay 10s, expect timeout..." << std::endl;

        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        auto start = std::chrono::steady_clock::now();

        client_->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                if (error != 0) {
                    std::cout << "✓ Request timed out as expected after " << duration.count() << "ms" << std::endl;
                    std::cout << "  Error code: " << error << std::endl;
                } else {
                    std::cout << "✗ Request succeeded unexpectedly" << std::endl;
                }

                completed = true;
            });

        // Wait for completion
        for (int i = 0; i < 200 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Test 2: Error response handling
    void TestErrorResponse() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test 2: Error Response Handling" << std::endl;
        std::cout << "========================================" << std::endl;

        std::string url = base_url_ + "/error";
        std::cout << "Requesting: " << url << std::endl;

        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        client_->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error != 0) {
                    std::cout << "✗ Request failed with error: " << error << std::endl;
                    completed = true;
                    return;
                }

                int status = response->GetStatusCode();
                std::string body = response->GetBodyAsString();

                std::cout << "Received HTTP " << status << std::endl;
                std::cout << "Body: " << body << std::endl;

                if (status == 500) {
                    std::cout << "✓ Error response handled correctly" << std::endl;
                    LogError("HTTP 500: " + body);
                } else {
                    std::cout << "✗ Unexpected status code" << std::endl;
                }

                completed = true;
            });

        // Wait for completion
        for (int i = 0; i < 100 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Test 3: Retry with exponential backoff
    void TestRetryLogic() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test 3: Retry with Exponential Backoff" << std::endl;
        std::cout << "========================================" << std::endl;

        std::string url = base_url_ + "/normal";
        int max_attempts = 3;

        for (int attempt = 1; attempt <= max_attempts; ++attempt) {
            std::cout << "Attempt " << attempt << "/" << max_attempts << std::endl;

            // Simulate failure for first 2 attempts
            if (attempt < max_attempts) {
                std::cout << "  Simulating failure..." << std::endl;

                int delay_ms = 1000 * (1 << (attempt - 1));  // Exponential: 1s, 2s, 4s...
                std::cout << "  Waiting " << delay_ms << "ms before retry..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                continue;
            }

            // Last attempt - actually make request
            auto request = quicx::IRequest::Create();
            std::atomic<bool> completed{false};

            client_->DoRequest(
                url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                    if (error != 0) {
                        std::cout << "  ✗ Request failed: " << error << std::endl;
                    } else {
                        std::cout << "  ✓ Success!" << std::endl;
                        std::cout << "  Response: " << response->GetBodyAsString() << std::endl;
                    }
                    completed = true;
                });

            // Wait for completion
            for (int i = 0; i < 100 && !completed; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    // Test 4: Large response handling
    void TestLargeResponse() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test 4: Large Response Handling" << std::endl;
        std::cout << "========================================" << std::endl;

        std::string url = base_url_ + "/large";
        std::cout << "Requesting: " << url << std::endl;
        std::cout << "Expecting 1MB response..." << std::endl;

        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        auto start = std::chrono::steady_clock::now();

        client_->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                if (error != 0) {
                    std::cout << "✗ Request failed: " << error << std::endl;
                    completed = true;
                    return;
                }

                std::string body = response->GetBodyAsString();
                size_t size = body.size();

                std::cout << "✓ Received " << size << " bytes in " << duration.count() << "ms" << std::endl;
                std::cout << "  Speed: " << (size / 1024.0 / 1024.0) / (duration.count() / 1000.0) << " MB/s"
                          << std::endl;

                completed = true;
            });

        // Wait for completion
        for (int i = 0; i < 100 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Test 5: Normal request
    void TestNormalRequest() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test 5: Normal Request" << std::endl;
        std::cout << "========================================" << std::endl;

        std::string url = base_url_ + "/normal";
        std::cout << "Requesting: " << url << std::endl;

        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        client_->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error != 0) {
                    std::cout << "✗ Request failed: " << error << std::endl;
                } else {
                    std::cout << "✓ HTTP " << response->GetStatusCode() << std::endl;
                    std::cout << "  Body: " << response->GetBodyAsString() << std::endl;
                }
                completed = true;
            });

        // Wait for completion
        for (int i = 0; i < 100 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void RunAllTests() {
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║  Error Handling Test Suite            ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝" << std::endl;

        TestNormalRequest();
        TestErrorResponse();
        TestRetryLogic();
        TestLargeResponse();
        TestConnectionTimeout();  // Run this last as it takes longest

        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests completed!" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }

private:
    void LogError(const std::string& error_msg) {
        std::ofstream log_file("error.log", std::ios::app);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        log_file << std::ctime(&time_t) << ": " << error_msg << std::endl;
        std::cout << "  Logged to error.log" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <scenario> [url]" << std::endl;
        std::cout << "Scenarios:" << std::endl;
        std::cout << "  all      - Run all test scenarios" << std::endl;
        std::cout << "  timeout  - Test connection timeout" << std::endl;
        std::cout << "  error    - Test error response" << std::endl;
        std::cout << "  retry    - Test retry logic" << std::endl;
        std::cout << "  large    - Test large response" << std::endl;
        std::cout << "  normal   - Test normal request" << std::endl;
        std::cout << "\nExample: " << argv[0] << " all https://localhost:8443" << std::endl;
        return 1;
    }

    std::string scenario = argv[1];
    std::string base_url = argc > 2 ? argv[2] : "https://localhost:8443";

    ErrorHandlingClient client(base_url);

    if (!client.Init()) {
        return 1;
    }

    if (scenario == "all") {
        client.RunAllTests();
    } else if (scenario == "timeout") {
        client.TestConnectionTimeout();
    } else if (scenario == "error") {
        client.TestErrorResponse();
    } else if (scenario == "retry") {
        client.TestRetryLogic();
    } else if (scenario == "large") {
        client.TestLargeResponse();
    } else if (scenario == "normal") {
        client.TestNormalRequest();
    } else {
        std::cerr << "Unknown scenario: " << scenario << std::endl;
        return 1;
    }

    return 0;
}
