#include <thread>
#include <chrono>
#include <iostream>
#include "http3/include/if_client.h"


int main() {
    quicx::http3::Http3Settings settings = quicx::http3::kDefaultHttp3Settings;
    settings.enable_push = 1;
    auto client = quicx::http3::IClient::Create(settings);

    client->Init(1, quicx::http3::LogLevel::kDebug);

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
                std::cout << "get push promise. header:" << iter.first << "value:" << iter.second << std::endl;
            }
            return true;
        }
    );

    client->SetPushHandler(
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            std::cout << "push status: " << response->GetStatusCode() << std::endl;
            std::cout << "push response: " << response->GetBody() << std::endl;
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}