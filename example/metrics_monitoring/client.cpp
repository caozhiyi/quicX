#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <sstream>
#include <thread>
#include <vector>

std::atomic<int> total_requests{0};
std::atomic<int> successful_requests{0};
std::atomic<int> failed_requests{0};

// Test 2 fires SendRequest() from 10 concurrent std::threads, and Test 3's
// response callback (plus a possible late-arriving Test 1/2 callback, see
// below) can run on the client library's own I/O thread at the same time as
// the main thread is printing its own status lines. std::cout is not
// synchronized across threads by default, so every write in this file must
// go through this mutex - otherwise concurrent writes interleave byte by
// byte and corrupt the printed text (confirmed with ThreadSanitizer: without
// this guard, output such as "custom_request_duration_ms" could come out as
// "custom_Bzuest_duration_ms").
std::mutex g_cout_mutex;

void SendRequest(const std::string& url, const std::string& endpoint_name) {
    // Synchronization state for waiting on the response. These are held via
    // shared_ptr and captured *by value* in the callback below so that if
    // the 10s wait_for() times out and this function returns while the
    // underlying request is still in flight, the eventually-late callback
    // keeps these objects alive instead of touching destroyed stack memory.
    auto mtx = std::make_shared<std::mutex>();
    auto cv = std::make_shared<std::condition_variable>();
    auto response_received = std::make_shared<std::atomic<bool>>(false);

    auto client = quicx::IClient::Create();

    quicx::Http3ClientConfig config;
    config.quic_config_.verify_peer_ = false;                         // examples use self-signed certs
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kWarn;  // Reduce noise
    client->Init(config);

    auto request = quicx::IRequest::Create();

    total_requests++;

    client->DoRequest(url, quicx::HttpMethod::kGet, request,
        [endpoint_name, mtx, cv, response_received](std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
            {
                std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
                if (error == 0) {
                    successful_requests++;
                    std::cout << " [OK] " << endpoint_name << " - Status: " << resp->GetStatusCode() << std::endl;
                } else {
                    failed_requests++;
                    std::cout << " [FAIL] " << endpoint_name << " - Error: " << error << std::endl;
                }
            }

            // Notify main thread that response is received
            {
                std::lock_guard<std::mutex> lock(*mtx);
                *response_received = true;
            }
            cv->notify_one();
        });

    // Wait for response, max 10 seconds
    {
        std::unique_lock<std::mutex> lock(*mtx);
        if (!cv->wait_for(lock, std::chrono::seconds(10), [&] { return response_received->load(); })) {
            {
                std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
                std::cout << " [TIMEOUT] " << endpoint_name << " - Timeout" << std::endl;
            }
            failed_requests++;
        }
    }
}

void PrintStats() {
    std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
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
    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << " quicX Metrics Monitoring - Test Client" << std::endl;
        std::cout << "=========================================\n" << std::endl;
    }

    std::string base_url = "https://127.0.0.1:7010";

    if (argc > 1) {
        base_url = argv[1];
    }

    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << " Target server: " << base_url << "\n" << std::endl;

        // Test 1: Basic functionality test
        std::cout << " Test 1: Basic Functionality" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
    }

    SendRequest(base_url + "/hello", "GET /hello");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SendRequest(base_url + "/slow", "GET /slow");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SendRequest(base_url + "/error", "GET /error");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    PrintStats();

    // Test 2: Load test
    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << " Test 2: Load Test (10 concurrent requests)" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
    }

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
    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << " Test 3: Fetching Metrics" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
    }

    auto metrics_client = quicx::IClient::Create();
    quicx::Http3ClientConfig metrics_config;
    metrics_config.quic_config_.verify_peer_ = false;  // examples use self-signed certs
    metrics_config.quic_config_.config_.log_level_ = quicx::LogLevel::kWarn;
    metrics_client->Init(metrics_config);

    auto metrics_request = quicx::IRequest::Create();

    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << "Fetching metrics from " << base_url << "/metrics..." << std::endl;
    }

    // Synchronization state, held via shared_ptr for the same reason as in
    // SendRequest(): a late callback must not touch destroyed stack memory
    // if the wait below times out first.
    auto metrics_mtx = std::make_shared<std::mutex>();
    auto metrics_cv = std::make_shared<std::condition_variable>();
    auto metrics_received = std::make_shared<std::atomic<bool>>(false);

    metrics_client->DoRequest(base_url + "/metrics", quicx::HttpMethod::kGet, metrics_request,
        [metrics_mtx, metrics_cv, metrics_received](std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
            {
                std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
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
            }

            // Notify main thread that response is received
            {
                std::lock_guard<std::mutex> lock(*metrics_mtx);
                *metrics_received = true;
            }
            metrics_cv->notify_one();
        });

    // Wait for response. The metrics connection is opened right after the
    // 10-concurrent load test; under that condition its QUIC handshake packets
    // are delayed ~8s (dropped and only recovered via PTO retransmission), so
    // the full exchange lands well past a 10s budget and the client times out
    // before the server's response arrives. Allow 30s to absorb that delay.
    {
        std::unique_lock<std::mutex> lock(*metrics_mtx);
        if (!metrics_cv->wait_for(lock, std::chrono::seconds(30), [&] { return metrics_received->load(); })) {
            std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
            std::cout << " [TIMEOUT] Metrics fetch timeout" << std::endl;
        }
    }

    // Final summary
    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << " Test completed!" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "\n Next steps:" << std::endl;
        std::cout << "   1. View metrics: quicx-curl " << base_url << "/metrics" << std::endl;
        std::cout << "   2. View dashboard: quicx-curl " << base_url << "/dashboard > dashboard.html" << std::endl;
        std::cout << "   3. Integrate with Prometheus for monitoring\n" << std::endl;
    }

    return 0;
}
