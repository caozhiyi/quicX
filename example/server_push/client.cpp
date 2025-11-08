#include <thread>
#include <chrono>
#include <iostream>
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"


int main() {
    quicx::http3::Http3Settings settings = quicx::http3::kDefaultHttp3Settings;
    settings.enable_push = 1;
    auto client = quicx::http3::IClient::Create(settings);

    quicx::http3::Http3Config config;
    config.thread_num_ = 1;
    config.log_level_ = quicx::http3::LogLevel::kError;
    client->Init(config);

    auto request = quicx::http3::IRequest::Create();
    request->SetBody("hello world");
    client->DoRequest(
        "https://127.0.0.1:8882/hello",
        quicx::http3::HttpMethod::kGet,
        request, 
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBody() << std::endl;
        }
    );
    client->SetPushPromiseHandler(
        [](std::unordered_map<std::string, std::string>& headers)->bool{
            for (auto iter : headers) {
                std::cout << "get push promise. header:" << iter.first << " value:" << iter.second << std::endl;
            }
            std::cout << std::endl;
            return true;
        }
    );

    client->SetPushHandler(
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            std::cout << "push status: " << response->GetStatusCode() << std::endl;
            std::cout << "push response: " << response->GetBody() << std::endl;
        }
    );

    // wait one second to make sure the first request is done
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // change push promise handler, return false to cancel push
    client->SetPushPromiseHandler(
        [](std::unordered_map<std::string, std::string>& headers)->bool{
            for (auto iter : headers) {
                std::cout << "get push promise. header:" << iter.first << " value:" << iter.second << std::endl;
            }
            std::cout << std::endl;
            return false;
        }
    );

    // do request again, push promise will be canceled
    client->DoRequest(
        "https://127.0.0.1:8882/hello",
        quicx::http3::HttpMethod::kGet,
        request, 
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBody() << std::endl;
        }
    );

    // wait one second to make sure the second request is done
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}