#include <thread>
#include <chrono>
#include <iostream>
#include <atomic>
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

std::atomic<int> pending_requests(0);

void WaitForCompletion() {
    while (pending_requests.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 2;
    config.log_level_ = quicx::LogLevel::kDebug;
    client->Init(config);

    std::cout << "==================================" << std::endl;
    std::cout << "RESTful API Client Demo" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    // Test 1: GET all users
    std::cout << "Test 1: GET /users - Get all users" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request1 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users",
        quicx::HttpMethod::kGet,
        request1,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
                
                std::string content_type;
                if (response->GetHeader("Content-Type", content_type)) {
                    std::cout << "Content-Type: " << content_type << std::endl;
                }
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 2: GET single user
    std::cout << "Test 2: GET /users/1 - Get user with ID 1" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request2 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users/1",
        quicx::HttpMethod::kGet,
        request2,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 3: POST - Create new user
    std::cout << "Test 3: POST /users - Create new user" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request3 = quicx::IRequest::Create();
    request3->AddHeader("Content-Type", "application/json");
    request3->AppendBody(std::string("{\"name\":\"David\",\"email\":\"david@example.com\",\"age\":28}"));
    
    client->DoRequest(
        "https://127.0.0.1:8883/users",
        quicx::HttpMethod::kPost,
        request3,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
                
                std::string location;
                if (response->GetHeader("Location", location)) {
                    std::cout << "Location: " << location << std::endl;
                }
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 4: PUT - Update user
    std::cout << "Test 4: PUT /users/2 - Update user with ID 2" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request4 = quicx::IRequest::Create();
    request4->AddHeader("Content-Type", "application/json");
    request4->AppendBody(std::string("{\"name\":\"Bob Smith\",\"email\":\"bob.smith@example.com\",\"age\":31}"));
    
    client->DoRequest(
        "https://127.0.0.1:8883/users/2",
        quicx::HttpMethod::kPut,
        request4,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 5: GET updated user to verify
    std::cout << "Test 5: GET /users/2 - Verify update" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request5 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users/2",
        quicx::HttpMethod::kGet,
        request5,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 6: GET statistics
    std::cout << "Test 6: GET /stats - Get statistics" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request6 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/stats",
        quicx::HttpMethod::kGet,
        request6,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 7: DELETE user
    std::cout << "Test 7: DELETE /users/3 - Delete user with ID 3" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request7 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users/3",
        quicx::HttpMethod::kDelete,
        request7,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                if (response->GetStatusCode() != 204) {
                    std::cout << "Response: " << response->GetBodyAsString() << std::endl;
                } else {
                    std::cout << "Response: (No Content)" << std::endl;
                }
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 8: Try to get deleted user (should return 404)
    std::cout << "Test 8: GET /users/3 - Try to get deleted user" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request8 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users/3",
        quicx::HttpMethod::kGet,
        request8,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 9: GET all users again to see final state
    std::cout << "Test 9: GET /users - Get all users (final state)" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request9 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users",
        quicx::HttpMethod::kGet,
        request9,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    // Test 10: Test error handling - Invalid ID
    std::cout << "Test 10: GET /users/invalid - Test error handling" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    pending_requests++;
    auto request10 = quicx::IRequest::Create();
    client->DoRequest(
        "https://127.0.0.1:8883/users/invalid",
        quicx::HttpMethod::kGet,
        request10,
        [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "Status: " << response->GetStatusCode() << std::endl;
                std::cout << "Response: " << response->GetBodyAsString() << std::endl;
            } else {
                std::cout << "Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
    WaitForCompletion();

    std::cout << "==================================" << std::endl;
    std::cout << "All tests completed!" << std::endl;
    std::cout << "==================================" << std::endl;

    return 0;
}

