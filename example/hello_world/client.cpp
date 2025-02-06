#include <thread>
#include <chrono>
#include <iostream>
#include "http3/include/if_client.h"


int main() {
    auto client = quicx::http3::IClient::Create();

    client->Init(1, quicx::http3::LL_DEBUG);

    auto request = quicx::http3::IRequest::Create();
    request->SetBody("hello world");
    client->DoRequest(
        "https://127.0.0.1:8882/hello",
        quicx::http3::HttpMethod::HM_GET,
        request, 
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            std::cout << "status: " << response->GetStatusCode() << std::endl;
            std::cout << "response: " << response->GetBody() << std::endl;
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}