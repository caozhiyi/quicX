#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

struct LoadTestConfig {
    std::string url;
    int num_clients = 10;
    int requests_per_client = 100;
    int rampup_seconds = 0;
    int duration_seconds = 0;  // 0 = use requests_per_client
};

struct LoadTestMetrics {
    std::atomic<int> total_requests{0};
    std::atomic<int> successful_requests{0};
    std::atomic<int> failed_requests{0};
    std::vector<double> latencies;
    std::mutex latencies_mutex;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
};

class LoadTester {
private:
    LoadTestConfig config_;
    LoadTestMetrics metrics_;
    std::atomic<bool> running_{true};

public:
    LoadTester(const LoadTestConfig& config):
        config_(config) {}

    void RunTest() {
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║  Load Testing Tool                     ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

        PrintConfig();

        metrics_.start_time = std::chrono::steady_clock::now();

        // Start progress monitor
        std::thread monitor_thread([this]() { MonitorProgress(); });

        // Create client threads
        std::vector<std::thread> client_threads;

        for (int i = 0; i < config_.num_clients; ++i) {
            // Ramp-up delay
            if (config_.rampup_seconds > 0) {
                int delay_ms = (config_.rampup_seconds * 1000 * i) / config_.num_clients;
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            client_threads.emplace_back([this, i]() { RunClient(i); });
        }

        // Wait for all clients
        for (auto& thread : client_threads) {
            thread.join();
        }

        running_ = false;
        metrics_.end_time = std::chrono::steady_clock::now();

        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }

        PrintResults();
    }

private:
    void RunClient(int client_id) {
        auto client = quicx::IClient::Create();

        quicx::Http3Config config;
        config.thread_num_ = 1;
        config.log_level_ = quicx::LogLevel::kError;
        config.connection_timeout_ms_ = 10000;

        if (!client->Init(config)) {
            std::cerr << "Client " << client_id << " failed to initialize" << std::endl;
            return;
        }

        int requests_to_send = config_.requests_per_client;
        auto client_start = std::chrono::steady_clock::now();

        for (int i = 0; i < requests_to_send && running_; ++i) {
            // Check duration limit
            if (config_.duration_seconds > 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - metrics_.start_time).count();

                if (elapsed >= config_.duration_seconds) {
                    break;
                }
            }

            auto request = quicx::IRequest::Create();
            request->AppendBody("hello world.");
            auto req_start = std::chrono::high_resolution_clock::now();
            std::atomic<bool> completed{false};

            metrics_.total_requests++;

            client->DoRequest(config_.url, quicx::HttpMethod::kGet, request,
                [this, req_start, &completed](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                    auto req_end = std::chrono::high_resolution_clock::now();
                    double latency_ms = std::chrono::duration<double, std::milli>(req_end - req_start).count();

                    if (error == 0) {
                        metrics_.successful_requests++;

                        std::lock_guard<std::mutex> lock(metrics_.latencies_mutex);
                        metrics_.latencies.push_back(latency_ms);
                    } else {
                        metrics_.failed_requests++;
                    }

                    completed = true;
                });

            // Wait for completion
            for (int j = 0; j < 100 && !completed; ++j) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void MonitorProgress() {
        const int bar_width = 40;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            int total = metrics_.total_requests.load();
            int successful = metrics_.successful_requests.load();
            int expected_total = config_.num_clients * config_.requests_per_client;

            if (expected_total == 0) continue;

            double progress = static_cast<double>(total) / expected_total;
            int filled = static_cast<int>(progress * bar_width);

            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - metrics_.start_time).count();

            double rps = elapsed_sec > 0 ? successful / elapsed_sec : 0;

            double avg_latency = 0;
            {
                std::lock_guard<std::mutex> lock(metrics_.latencies_mutex);
                if (!metrics_.latencies.empty()) {
                    for (double lat : metrics_.latencies) avg_latency += lat;
                    avg_latency /= metrics_.latencies.size();
                }
            }

            std::cout << "\r[";
            for (int i = 0; i < bar_width; ++i) {
                if (i < filled)
                    std::cout << "=";
                else if (i == filled)
                    std::cout << ">";
                else
                    std::cout << " ";
            }
            std::cout << "] " << std::fixed << std::setprecision(0) << (progress * 100) << "% | ";
            std::cout << total << "/" << expected_total << " | ";
            std::cout << std::setprecision(0) << rps << " req/s | ";
            std::cout << "Avg: " << std::setprecision(1) << avg_latency << "ms";
            std::cout << std::flush;
        }

        std::cout << std::endl;
    }

    void PrintConfig() {
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Target: " << config_.url << std::endl;
        std::cout << "  Clients: " << config_.num_clients << std::endl;

        if (config_.duration_seconds > 0) {
            std::cout << "  Duration: " << config_.duration_seconds << "s" << std::endl;
        } else {
            std::cout << "  Requests per client: " << config_.requests_per_client << std::endl;
            std::cout << "  Total requests: " << (config_.num_clients * config_.requests_per_client) << std::endl;
        }

        if (config_.rampup_seconds > 0) {
            std::cout << "  Ramp-up: " << config_.rampup_seconds << "s" << std::endl;
        }

        std::cout << "\nStarting load test...\n" << std::endl;
    }

    void PrintResults() {
        double duration_sec = std::chrono::duration<double>(metrics_.end_time - metrics_.start_time).count();

        int total = metrics_.total_requests.load();
        int successful = metrics_.successful_requests.load();
        int failed = metrics_.failed_requests.load();

        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Results" << std::endl;
        std::cout << "========================================" << std::endl;

        std::cout << "\nRequests:" << std::endl;
        std::cout << "  Total: " << total << std::endl;
        std::cout << "  Successful: " << successful << " (" << std::fixed << std::setprecision(2)
                  << (100.0 * successful / total) << "%)" << std::endl;
        std::cout << "  Failed: " << failed << " (" << (100.0 * failed / total) << "%)" << std::endl;

        std::cout << "\nPerformance:" << std::endl;
        std::cout << "  Duration: " << std::setprecision(1) << duration_sec << "s" << std::endl;
        std::cout << "  Throughput: " << std::setprecision(1) << (successful / duration_sec) << " req/s" << std::endl;

        // Latency statistics
        std::lock_guard<std::mutex> lock(metrics_.latencies_mutex);
        if (!metrics_.latencies.empty()) {
            std::sort(metrics_.latencies.begin(), metrics_.latencies.end());

            double avg = 0;
            for (double lat : metrics_.latencies) avg += lat;
            avg /= metrics_.latencies.size();

            double p50 = metrics_.latencies[metrics_.latencies.size() / 2];
            double p95 = metrics_.latencies[metrics_.latencies.size() * 95 / 100];
            double p99 = metrics_.latencies[metrics_.latencies.size() * 99 / 100];

            std::cout << "\nLatency:" << std::endl;
            std::cout << "  Average: " << std::setprecision(1) << avg << "ms" << std::endl;
            std::cout << "  P50: " << p50 << "ms" << std::endl;
            std::cout << "  P95: " << p95 << "ms" << std::endl;
            std::cout << "  P99: " << p99 << "ms" << std::endl;
            std::cout << "  Min: " << metrics_.latencies.front() << "ms" << std::endl;
            std::cout << "  Max: " << metrics_.latencies.back() << "ms" << std::endl;
        }

        std::cout << "\n========================================\n" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <url> [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --clients <N>      Number of concurrent clients (default: 10)" << std::endl;
        std::cout << "  --requests <N>     Requests per client (default: 100)" << std::endl;
        std::cout << "  --rampup <seconds> Ramp-up time (default: 0)" << std::endl;
        std::cout << "  --duration <seconds> Test duration (default: 0, use requests)" << std::endl;
        std::cout << "\nExample:" << std::endl;
        std::cout << "  " << argv[0] << " https://localhost:8443/hello --clients 50 --requests 100" << std::endl;
        return 1;
    }

    LoadTestConfig config;
    config.url = argv[1];

    // Parse options
    for (int i = 2; i < argc; i += 2) {
        std::string opt = argv[i];
        if (i + 1 >= argc) break;

        if (opt == "--clients") {
            config.num_clients = std::atoi(argv[i + 1]);
        } else if (opt == "--requests") {
            config.requests_per_client = std::atoi(argv[i + 1]);
        } else if (opt == "--rampup") {
            config.rampup_seconds = std::atoi(argv[i + 1]);
        } else if (opt == "--duration") {
            config.duration_seconds = std::atoi(argv[i + 1]);
        }
    }

    LoadTester tester(config);
    tester.RunTest();

    return 0;
}
