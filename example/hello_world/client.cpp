#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

int main() {
    auto client = quicx::IClient::Create();

    quicx::Http3ClientConfig config;
    config.quic_config_.verify_peer_ = false;  // examples use self-signed certs
    config.quic_config_.config_.worker_thread_num_ = 1;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;
    client->Init(config);

    // wait for response
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> response_received{false};

    // record request start time
    auto start_time = std::chrono::high_resolution_clock::now();

    auto request = quicx::IRequest::Create();
    request->AppendBody(std::string("hello world"));
    client->DoRequest("https://127.0.0.1:7001/hello", quicx::HttpMethod::kGet, request,
        [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            // Calculate request latency
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

            if (error != 0) {
                std::cout << "Request failed with error: " << error << std::endl;
            } else {
                std::cout << "======== Response Received ========" << std::endl;
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Body: " << response->GetBodyAsString() << std::endl;
                std::cout << "===================================" << std::endl;
            }
            std::cout << "Request took: " << duration.count() << " ms" << std::endl;

            // notify main thread response received
            {
                std::lock_guard<std::mutex> lock(mtx);
                response_received = true;
            }
            cv.notify_one();
        });

    // wait for response, max wait 10 seconds
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return response_received.load(); })) {
            std::cout << "Request timeout after 10 seconds" << std::endl;
        }
    }

    std::cout << "Client exiting..." << std::endl;
    return 0;
}