#include <chrono>
#include <iostream>
#include <thread>
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

int main() {
    quicx::Http3Settings settings = quicx::kDefaultHttp3Settings;
    auto client = quicx::IClient::Create(settings);

    quicx::Http3ClientConfig config;
    config.quic_config_.config_.worker_thread_num_ = 1;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;
    config.enable_push_ = true;  // Enable server push
    client->Init(config);

    auto request = quicx::IRequest::Create();
    request->AppendBody("hello world");
    client->DoRequest("https://127.0.0.1:7008/hello", quicx::HttpMethod::kGet, request,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBodyAsString() << std::endl;
        });
    client->SetPushPromiseHandler([](std::unordered_map<std::string, std::string>& headers) -> bool {
        for (auto iter : headers) {
            std::cout << "get push promise. header:" << iter.first << " value:" << iter.second << std::endl;
        }
        std::cout << std::endl;
        return true;
    });

    client->SetPushHandler([](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
        std::cout << "push status: " << response->GetStatusCode() << std::endl;
        std::cout << "push response: " << response->GetBodyAsString() << std::endl;
    });

    // wait one second to make sure the first request is done
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // change push promise handler, return false to cancel push
    client->SetPushPromiseHandler([](std::unordered_map<std::string, std::string>& headers) -> bool {
        for (auto iter : headers) {
            std::cout << "get push promise. header:" << iter.first << " value:" << iter.second << std::endl;
        }
        std::cout << std::endl;
        return false;
    });

    // Create a NEW request object for the second request
    // The previous request's body has been consumed by SendBodyDirectly()
    auto request2 = quicx::IRequest::Create();
    request2->AppendBody("hello world");

    // do request again, push promise will be canceled
    client->DoRequest("https://127.0.0.1:7008/hello", quicx::HttpMethod::kGet, request2,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBodyAsString() << std::endl;
        });

    // wait one second to make sure the second request is done
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}