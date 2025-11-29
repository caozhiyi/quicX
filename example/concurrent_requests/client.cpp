#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

struct RequestResult {
    int id;
    std::string endpoint;
    int status_code;
    std::string response;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    int64_t duration_ms;
    bool success;
};

class ConcurrentTester {
private:
    quicx::IClient* client_;
    std::vector<RequestResult> results_;
    std::mutex results_mutex_;
    std::atomic<int> pending_requests_{0};
    std::atomic<int> completed_requests_{0};
    std::chrono::steady_clock::time_point test_start_time_;

public:
    ConcurrentTester(quicx::IClient* client):
        client_(client) {
        test_start_time_ = std::chrono::steady_clock::now();
    }

    void SendRequest(int id, const std::string& endpoint, const std::string& url) {
        pending_requests_++;

        auto start = std::chrono::steady_clock::now();
        auto request = quicx::IRequest::Create();

        client_->DoRequest(url, quicx::HttpMethod::kGet, request,
            [this, id, endpoint, start](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                RequestResult result;
                result.id = id;
                result.endpoint = endpoint;
                result.start_time = start;
                result.end_time = end;
                result.duration_ms = duration.count();
                result.success = (error == 0);
                result.status_code = error == 0 ? response->GetStatusCode() : 0;
                result.response = error == 0 ? response->GetBodyAsString() : "";

                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_.push_back(result);
                }

                completed_requests_++;
                pending_requests_--;
            });
    }

    void WaitForCompletion() {
        while (pending_requests_.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void PrintResults() {
        std::lock_guard<std::mutex> lock(results_mutex_);

        if (results_.empty()) {
            std::cout << "No results to display" << std::endl;
            return;
        }

        // Sort by start time
        std::sort(results_.begin(), results_.end(),
            [](const RequestResult& a, const RequestResult& b) { return a.start_time < b.start_time; });

        auto first_start = results_[0].start_time;
        auto last_end = results_[0].end_time;

        for (const auto& result : results_) {
            if (result.end_time > last_end) {
                last_end = result.end_time;
            }
        }

        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(last_end - first_start);

        std::cout << "\n========================================" << std::endl;
        std::cout << "CONCURRENT REQUEST RESULTS" << std::endl;
        std::cout << "========================================" << std::endl;

        // Print individual results
        std::cout << "\nIndividual Requests:" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::left << std::setw(6) << "ID" << std::setw(15) << "Endpoint" << std::setw(10) << "Status"
                  << std::setw(12) << "Duration(ms)" << std::setw(12) << "Start(ms)" << std::setw(12) << "End(ms)"
                  << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        for (const auto& result : results_) {
            auto start_offset = std::chrono::duration_cast<std::chrono::milliseconds>(result.start_time - first_start);
            auto end_offset = std::chrono::duration_cast<std::chrono::milliseconds>(result.end_time - first_start);

            std::cout << std::left << std::setw(6) << result.id << std::setw(15) << result.endpoint << std::setw(10)
                      << (result.success ? std::to_string(result.status_code) : "Error") << std::setw(12)
                      << result.duration_ms << std::setw(12) << start_offset.count() << std::setw(12)
                      << end_offset.count() << std::endl;
        }

        // Calculate statistics
        int64_t total_req_duration = 0;
        int64_t min_duration = results_[0].duration_ms;
        int64_t max_duration = results_[0].duration_ms;
        int success_count = 0;

        for (const auto& result : results_) {
            total_req_duration += result.duration_ms;
            if (result.duration_ms < min_duration) min_duration = result.duration_ms;
            if (result.duration_ms > max_duration) max_duration = result.duration_ms;
            if (result.success) success_count++;
        }

        double avg_duration = static_cast<double>(total_req_duration) / results_.size();

        std::cout << "\nStatistics:" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << "Total Requests:       " << results_.size() << std::endl;
        std::cout << "Successful Requests:  " << success_count << std::endl;
        std::cout << "Failed Requests:      " << (results_.size() - success_count) << std::endl;
        std::cout << "Total Wall Time:      " << total_duration.count() << " ms" << std::endl;
        std::cout << "Sum of All Durations: " << total_req_duration << " ms" << std::endl;
        std::cout << "Min Request Duration: " << min_duration << " ms" << std::endl;
        std::cout << "Max Request Duration: " << max_duration << " ms" << std::endl;
        std::cout << "Avg Request Duration: " << std::fixed << std::setprecision(2) << avg_duration << " ms"
                  << std::endl;

        // Calculate efficiency
        double efficiency = (static_cast<double>(total_req_duration) / total_duration.count()) * 100.0;
        std::cout << "\nMultiplexing Efficiency:" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << "Parallelization:      " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
        std::cout << "Speedup Factor:       " << std::fixed << std::setprecision(2)
                  << (static_cast<double>(total_req_duration) / total_duration.count()) << "x" << std::endl;

        if (total_duration.count() > 0) {
            std::cout << "Requests/Second:      " << std::fixed << std::setprecision(2)
                      << (static_cast<double>(results_.size()) / total_duration.count() * 1000.0) << std::endl;
        }

        std::cout << "\nVisualization (Timeline):" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        PrintTimeline(first_start, total_duration.count());

        std::cout << "========================================\n" << std::endl;
    }

    void PrintTimeline(const std::chrono::steady_clock::time_point& first_start, int64_t total_ms) {
        const int timeline_width = 70;

        for (const auto& result : results_) {
            auto start_offset =
                std::chrono::duration_cast<std::chrono::milliseconds>(result.start_time - first_start).count();
            auto end_offset =
                std::chrono::duration_cast<std::chrono::milliseconds>(result.end_time - first_start).count();

            int start_pos = (start_offset * timeline_width) / total_ms;
            int end_pos = (end_offset * timeline_width) / total_ms;
            int duration = end_pos - start_pos;
            if (duration < 1) duration = 1;

            std::cout << std::setw(3) << result.id << " |";
            std::cout << std::string(start_pos, ' ');
            std::cout << std::string(duration, '=');
            std::cout << std::endl;
        }
        std::cout << "    |" << std::string(timeline_width, '-') << "|" << std::endl;
        std::cout << "    0" << std::string(timeline_width - 10, ' ') << total_ms << "ms" << std::endl;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.clear();
        completed_requests_ = 0;
        test_start_time_ = std::chrono::steady_clock::now();
    }

    int GetResultCount() {
        std::lock_guard<std::mutex> lock(results_mutex_);
        return results_.size();
    }
};

void RunMixedConcurrentTest(ConcurrentTester& tester, const std::string& base_url) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: Mixed Concurrent Requests" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Sending 15 requests concurrently:" << std::endl;
    std::cout << "  - 5 fast (10ms each)" << std::endl;
    std::cout << "  - 5 medium (100ms each)" << std::endl;
    std::cout << "  - 5 slow (500ms each)" << std::endl;
    std::cout << "Expected sequential time: ~3050ms" << std::endl;
    std::cout << "Expected concurrent time: ~500ms" << std::endl;
    std::cout << std::endl;

    tester.Reset();

    int id = 1;

    // Send fast requests
    for (int i = 0; i < 5; i++) {
        tester.SendRequest(id++, "fast", base_url + "/fast");
    }

    // Send medium requests
    for (int i = 0; i < 5; i++) {
        tester.SendRequest(id++, "medium", base_url + "/medium");
    }

    // Send slow requests
    for (int i = 0; i < 5; i++) {
        tester.SendRequest(id++, "slow", base_url + "/slow");
    }

    std::cout << "Waiting for responses..." << std::endl;
    tester.WaitForCompletion();
    tester.PrintResults();
}

void RunBurstTest(ConcurrentTester& tester, const std::string& base_url) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Burst Request Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Sending 20 random delay requests simultaneously" << std::endl;
    std::cout << "Each request has random delay between 10-500ms" << std::endl;
    std::cout << std::endl;

    tester.Reset();

    for (int i = 1; i <= 20; i++) {
        tester.SendRequest(i, "random", base_url + "/random");
    }

    std::cout << "Waiting for responses..." << std::endl;
    tester.WaitForCompletion();
    tester.PrintResults();
}

void RunDataTransferTest(ConcurrentTester& tester, const std::string& base_url) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 3: Concurrent Data Transfer" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Downloading different sized data concurrently:" << std::endl;
    std::cout << "  - 10 KB, 50 KB, 100 KB, 500 KB, 1 MB" << std::endl;
    std::cout << std::endl;

    tester.Reset();

    int sizes[] = {10, 50, 100, 500, 1024};
    int id = 1;

    for (int size : sizes) {
        std::string endpoint = "data/" + std::to_string(size) + "KB";
        tester.SendRequest(id++, endpoint, base_url + "/data/" + std::to_string(size));
    }

    std::cout << "Waiting for responses..." << std::endl;
    tester.WaitForCompletion();
    tester.PrintResults();
}

void RunScalabilityTest(ConcurrentTester& tester, const std::string& base_url) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 4: Scalability Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Testing with increasing concurrency levels" << std::endl;
    std::cout << std::endl;

    int levels[] = {10, 25, 50, 100};

    for (int level : levels) {
        std::cout << "\nConcurrency Level: " << level << " requests" << std::endl;
        std::cout << std::string(40, '-') << std::endl;

        tester.Reset();

        for (int i = 1; i <= level; i++) {
            tester.SendRequest(i, "fast", base_url + "/fast");
        }

        auto start = std::chrono::steady_clock::now();
        tester.WaitForCompletion();
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Completed " << level << " requests in " << duration.count() << " ms" << std::endl;
        std::cout << "Throughput: " << std::fixed << std::setprecision(2)
                  << (static_cast<double>(level) / duration.count() * 1000.0) << " req/s" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 4;
    config.log_level_ = quicx::LogLevel::kError;
    client->Init(config);

    std::string base_url = "https://127.0.0.1:8885";

    std::cout << "==================================" << std::endl;
    std::cout << "HTTP/3 Concurrent Request Tester" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Server: " << base_url << std::endl;
    std::cout << "Client threads: 4" << std::endl;
    std::cout << std::endl;

    ConcurrentTester tester(client.get());

    // Run different tests
    RunMixedConcurrentTest(tester, base_url);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    RunBurstTest(tester, base_url);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    RunDataTransferTest(tester, base_url);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    RunScalabilityTest(tester, base_url);

    std::cout << "\n==================================" << std::endl;
    std::cout << "All tests completed!" << std::endl;
    std::cout << "==================================" << std::endl;

    return 0;
}
