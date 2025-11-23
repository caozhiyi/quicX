#include <thread>
#include <chrono>
#include <iostream>
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"


int main() {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 1;
    config.log_level_ = quicx::LogLevel::kInfo;
    client->Init(config);

    auto request = quicx::IRequest::Create();
    request->AppendBody(std::string("hello world"));
    client->DoRequest(
        "https://127.0.0.1:8882/hello",
        quicx::HttpMethod::kGet,
        request, 
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBodyAsString() << std::endl;
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}