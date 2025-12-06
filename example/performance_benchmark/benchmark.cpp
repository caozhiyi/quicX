#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

class PerformanceBenchmark {
private:
    std::shared_ptr<quicx::IClient> client_;
    std::string url_;

public:
    PerformanceBenchmark(const std::string& url):
        url_(url) {
        client_ = quicx::IClient::Create();

        quicx::Http3Config config;
        config.thread_num_ = 4;
        config.log_level_ = quicx::LogLevel::kError;  // Minimal logging for performance

        client_->Init(config);
    }

    void RunAllBenchmarks() {
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║  Performance Benchmark                 ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

        BenchmarkLatency();
        BenchmarkThroughput();
        BenchmarkConcurrency();

        std::cout << "\nBenchmark completed!" << std::endl;
    }

private:
    void BenchmarkLatency() {
        std::cout << "Latency Benchmark" << std::endl;
        std::cout << "========================================" << std::endl;

        const int num_requests = 100;
        std::vector<double> latencies;
        latencies.reserve(num_requests);

        std::cout << "Running " << num_requests << " requests..." << std::endl;

        for (int i = 0; i < num_requests; ++i) {
            auto request = quicx::IRequest::Create();
            std::atomic<bool> completed{false};
            double latency_ms = 0;

            auto start = std::chrono::high_resolution_clock::now();

            client_->DoRequest(url_, quicx::HttpMethod::kGet, request,
                [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                    auto end = std::chrono::high_resolution_clock::now();
                    latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
                    completed = true;
                });

            // Wait for completion
            for (int j = 0; j < 100 && !completed; ++j) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (completed) {
                latencies.push_back(latency_ms);
            }
        }

        // Calculate statistics
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());

            double avg = 0;
            for (double lat : latencies) avg += lat;
            avg /= latencies.size();

            double p50 = latencies[latencies.size() / 2];
            double p95 = latencies[latencies.size() * 95 / 100];
            double p99 = latencies[latencies.size() * 99 / 100];

            std::cout << "\nResults:" << std::endl;
            std::cout << "  Requests: " << latencies.size() << std::endl;
            std::cout << "  Average: " << std::fixed << std::setprecision(2) << avg << "ms" << std::endl;
            std::cout << "  P50: " << p50 << "ms" << std::endl;
            std::cout << "  P95: " << p95 << "ms" << std::endl;
            std::cout << "  P99: " << p99 << "ms" << std::endl;
            std::cout << "  Min: " << latencies.front() << "ms" << std::endl;
            std::cout << "  Max: " << latencies.back() << "ms" << std::endl;
        }
    }

    void BenchmarkThroughput() {
        std::cout << "\n\nThroughput Benchmark" << std::endl;
        std::cout << "========================================" << std::endl;

        const size_t target_bytes = 10 * 1024 * 1024;  // 10MB
        std::atomic<size_t> total_bytes{0};
        std::atomic<int> completed_requests{0};

        std::cout << "Downloading data..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        // Make multiple concurrent requests
        const int num_requests = 10;
        for (int i = 0; i < num_requests; ++i) {
            auto request = quicx::IRequest::Create();

            client_->DoRequest(url_, quicx::HttpMethod::kGet, request,
                [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                    if (error == 0) {
                        total_bytes += response->GetBodyAsString().size();
                    }
                    completed_requests++;
                });
        }

        // Wait for all requests
        while (completed_requests < num_requests) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        auto end = std::chrono::high_resolution_clock::now();
        double duration_sec = std::chrono::duration<double>(end - start).count();

        double throughput_mbps = (total_bytes / 1024.0 / 1024.0) / duration_sec;

        std::cout << "\nResults:" << std::endl;
        std::cout << "  Data transferred: " << std::fixed << std::setprecision(2) << (total_bytes / 1024.0 / 1024.0)
                  << " MB" << std::endl;
        std::cout << "  Time: " << duration_sec << "s" << std::endl;
        std::cout << "  Throughput: " << throughput_mbps << " MB/s" << std::endl;
    }

    void BenchmarkConcurrency() {
        std::cout << "\n\nConcurrency Benchmark" << std::endl;
        std::cout << "========================================" << std::endl;

        const int num_requests = 50;
        std::atomic<int> completed{0};

        std::cout << "Sending " << num_requests << " concurrent requests..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < num_requests; ++i) {
            auto request = quicx::IRequest::Create();

            client_->DoRequest(url_, quicx::HttpMethod::kGet, request,
                [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) { completed++; });
        }

        // Wait for all
        while (completed < num_requests) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto end = std::chrono::high_resolution_clock::now();
        double duration_sec = std::chrono::duration<double>(end - start).count();

        double rps = num_requests / duration_sec;

        std::cout << "\nResults:" << std::endl;
        std::cout << "  Concurrent requests: " << num_requests << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << duration_sec << "s" << std::endl;
        std::cout << "  Requests/second: " << rps << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <url>" << std::endl;
        std::cout << "Example: " << argv[0] << " https://localhost:8443/hello" << std::endl;
        return 1;
    }

    std::string url = argv[1];

    PerformanceBenchmark benchmark(url);
    benchmark.RunAllBenchmarks();

    return 0;
}
