#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

int main() {
    quicx::Http3Settings settings = quicx::kDefaultHttp3Settings;
    auto client = quicx::IClient::Create(settings);

    quicx::Http3ClientConfig config;
    config.quic_config_.verify_peer_ = false;  // examples use self-signed certs
    config.quic_config_.config_.worker_thread_num_ = 1;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;
    config.enable_push_ = true;  // Enable server push
    client->Init(config);

    // Synchronization for first request
    std::mutex mutex1;
    std::condition_variable cv1;
    bool completed1 = false;

    // Set push promise handler BEFORE sending request
    client->SetPushPromiseHandler([](std::unordered_map<std::string, std::string>& headers) -> bool {
        for (auto iter : headers) {
            std::cout << "get push promise. header:" << iter.first << " value:" << iter.second << std::endl;
        }
        std::cout << std::endl;
        return true;
    });

    // Set push handler BEFORE sending request
    client->SetPushHandler([](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
        std::cout << "push status: " << response->GetStatusCode() << std::endl;
        std::cout << "push response: " << response->GetBodyAsString() << std::endl;
    });

    auto request = quicx::IRequest::Create();
    request->AppendBody("hello world");
    client->DoRequest("https://127.0.0.1:7008/hello", quicx::HttpMethod::kGet, request,
        [&mutex1, &cv1, &completed1](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBodyAsString() << std::endl;
            
            // Notify completion
            {
                std::lock_guard<std::mutex> lock(mutex1);
                completed1 = true;
            }
            cv1.notify_one();
        });

    // Wait for the first request to complete
    {
        std::unique_lock<std::mutex> lock(mutex1);
        cv1.wait(lock, [&completed1] { return completed1; });
    }

    // Give some time for server push to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // change push promise handler, return false to cancel push
    client->SetPushPromiseHandler([](std::unordered_map<std::string, std::string>& headers) -> bool {
        for (auto iter : headers) {
            std::cout << "get push promise. header:" << iter.first << " value:" << iter.second << std::endl;
        }
        std::cout << std::endl;
        return false;
    });

    // Synchronization for second request
    std::mutex mutex2;
    std::condition_variable cv2;
    bool completed2 = false;

    // Create a NEW request object for the second request
    // The previous request's body has been consumed by SendBodyDirectly()
    auto request2 = quicx::IRequest::Create();
    request2->AppendBody("hello world");

    // do request again, push promise will be canceled
    client->DoRequest("https://127.0.0.1:7008/hello", quicx::HttpMethod::kGet, request2,
        [&mutex2, &cv2, &completed2](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBodyAsString() << std::endl;
            
            // Notify completion
            {
                std::lock_guard<std::mutex> lock(mutex2);
                completed2 = true;
            }
            cv2.notify_one();
        });

    // Wait for the second request to complete
    {
        std::unique_lock<std::mutex> lock(mutex2);
        cv2.wait(lock, [&completed2] { return completed2; });
    }

    // IMPORTANT: Wait for server push to arrive
    // Server uses a timer (kServerPushWaitTimeMs) to delay push sending
    // We need to wait long enough for:
    // 1. Server push timer to fire (10ms or 30000ms depending on config)
    // 2. Server to send the push data
    // 3. Client to receive and process the push
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Client finished, waiting for any pending push responses..." << std::endl;
}