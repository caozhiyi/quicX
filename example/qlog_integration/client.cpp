#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

int main() {
    // 2. Create Client
    auto client = quicx::IClient::Create();

    quicx::Http3ClientConfig config;
    config.quic_config_.verify_peer_ = false;  // examples use self-signed certs
    config.quic_config_.config_.worker_thread_num_ = 1;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;

    // Configure QLog via Http3Config
    config.quic_config_.config_.qlog_config_.enabled = true;
    config.quic_config_.config_.qlog_config_.output_dir = "./qlog_output_client";
    config.quic_config_.config_.qlog_config_.flush_interval_ms = 100;

    client->Init(config);

    std::cout << "[Client] QLog enabled at ./qlog_output_client" << std::endl;

    // 3. Make Request
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> response_received{false};

    auto start_time = std::chrono::high_resolution_clock::now();
    auto request = quicx::IRequest::Create();
    // Using GET request

    std::cout << "[Client] Sending request to https://127.0.0.1:7012/qlog" << std::endl;

    client->DoRequest("https://127.0.0.1:7012/qlog", quicx::HttpMethod::kGet, request,
        [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

            if (error != 0) {
                std::cout << "[Client] Request failed with error: " << error << std::endl;
            } else {
                std::cout << "======== Response Received ========" << std::endl;
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Body: " << response->GetBodyAsString() << std::endl;
                std::cout << "===================================" << std::endl;
            }
            std::cout << "[Client] Request took: " << duration.count() << " ms" << std::endl;

            {
                std::lock_guard<std::mutex> lock(mtx);
                response_received = true;
            }
            cv.notify_one();
        });

    // 4. Wait for completion
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return response_received.load(); })) {
            std::cout << "[Client] Request timeout" << std::endl;
        }
    }

    std::cout << "[Client] Exiting..." << std::endl;

    return 0;
}
