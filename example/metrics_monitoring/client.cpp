#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

std::atomic<int> total_requests{0};
std::atomic<int> successful_requests{0};
std::atomic<int> failed_requests{0};

void SendRequest(const std::string& url, const std::string& endpoint_name) {
    // Synchronization mechanism for waiting for response
    // IMPORTANT: Declare these BEFORE the client to ensure they outlive the client.
    // The client's destructor may wait for pending callbacks, which use these primitives.
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> response_received{false};

    auto client = quicx::IClient::Create();

    quicx::Http3ClientConfig config;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kWarn;  // Reduce noise
    client->Init(config);

    auto request = quicx::IRequest::Create();

    total_requests++;

    client->DoRequest(url, quicx::HttpMethod::kGet, request,
        [endpoint_name, &mtx, &cv, &response_received](std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
            if (error == 0) {
                successful_requests++;
                std::cout << " [OK] " << endpoint_name << " - Status: " << resp->GetStatusCode() << std::endl;
            } else {
                failed_requests++;
                std::cout << " [FAIL] " << endpoint_name << " - Error: " << error << std::endl;
            }

            // Notify main thread that response is received
            {
                std::lock_guard<std::mutex> lock(mtx);
                response_received = true;
            }
            cv.notify_one();
        });

    // Wait for response, max 10 seconds
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return response_received.load(); })) {
            std::cout << " [TIMEOUT] " << endpoint_name << " - Timeout" << std::endl;
            failed_requests++;
        }
    }
}

void PrintStats() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << " Test Statistics" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  Total Requests:      " << total_requests << std::endl;
    std::cout << "  Successful:          " << successful_requests << " [OK]" << std::endl;
    std::cout << "  Failed:              " << failed_requests << " [FAIL]" << std::endl;
    std::cout << "  Success Rate:        " << (total_requests > 0 ? (successful_requests * 100.0 / total_requests) : 0)
              << "%" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << " quicX Metrics Monitoring - Test Client" << std::endl;
    std::cout << "=========================================\n" << std::endl;

    std::string base_url = "https://127.0.0.1:7010";

    if (argc > 1) {
        base_url = argv[1];
    }

    std::cout << " Target server: " << base_url << "\n" << std::endl;

    // Test 1: Basic functionality test
    std::cout << " Test 1: Basic Functionality" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    SendRequest(base_url + "/hello", "GET /hello");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SendRequest(base_url + "/slow", "GET /slow");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SendRequest(base_url + "/error", "GET /error");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    PrintStats();

    // Test 2: Load test
    std::cout << " Test 2: Load Test (10 concurrent requests)" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    std::vector<std::thread> threads;

    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&base_url, i]() {
            std::string endpoint = (i % 3 == 0) ? "/hello" : (i % 3 == 1) ? "/slow" : "/error";
            SendRequest(base_url + endpoint, "GET " + endpoint);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    PrintStats();

    // Test 3: Fetch metrics
    std::cout << " Test 3: Fetching Metrics" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    auto metrics_client = quicx::IClient::Create();
    quicx::Http3ClientConfig metrics_config;
    metrics_config.quic_config_.config_.log_level_ = quicx::LogLevel::kWarn;
    metrics_client->Init(metrics_config);

    auto metrics_request = quicx::IRequest::Create();

    std::cout << "Fetching metrics from " << base_url << "/metrics..." << std::endl;

    // Synchronization mechanism for waiting for response
    std::mutex metrics_mtx;
    std::condition_variable metrics_cv;
    std::atomic<bool> metrics_received{false};

    metrics_client->DoRequest(base_url + "/metrics", quicx::HttpMethod::kGet, metrics_request,
        [&metrics_mtx, &metrics_cv, &metrics_received](std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
            if (error == 0) {
                std::cout << "\n" << std::string(60, '=') << std::endl;
                std::cout << " Server Metrics (Prometheus Format)" << std::endl;
                std::cout << std::string(60, '=') << std::endl;

                std::string body = resp->GetBodyAsString();

                // Display all metrics (standard metrics don't have quicx_ prefix)
                std::istringstream iss(body);
                std::string line;
                while (std::getline(iss, line)) {
                    std::cout << line << std::endl;
                }

                std::cout << std::string(60, '=') << std::endl;
            } else {
                std::cout << " [FAIL] Failed to fetch metrics: " << error << std::endl;
            }

            // Notify main thread that response is received
            {
                std::lock_guard<std::mutex> lock(metrics_mtx);
                metrics_received = true;
            }
            metrics_cv.notify_one();
        });

    // Wait for response, max 10 seconds
    {
        std::unique_lock<std::mutex> lock(metrics_mtx);
        if (!metrics_cv.wait_for(lock, std::chrono::seconds(10), [&] { return metrics_received.load(); })) {
            std::cout << " [TIMEOUT] Metrics fetch timeout" << std::endl;
        }
    }

    // Final summary
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << " Test completed!" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "\n Next steps:" << std::endl;
    std::cout << "   1. View metrics: quicx-curl " << base_url << "/metrics" << std::endl;
    std::cout << "   2. View dashboard: quicx-curl " << base_url << "/dashboard > dashboard.html" << std::endl;
    std::cout << "   3. Integrate with Prometheus for monitoring\n" << std::endl;

    return 0;
}
