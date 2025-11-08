#include <thread>
#include <chrono>
#include <iostream>
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"


int main() {
    auto client = quicx::http3::IClient::Create();

    quicx::http3::Http3Config config;
    config.thread_num_ = 1;
    config.log_level_ = quicx::http3::LogLevel::kInfo;
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

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}