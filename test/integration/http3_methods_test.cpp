// HTTP/3 Methods Integration Test
// Tests all HTTP methods (GET, POST, PUT, DELETE, HEAD) end-to-end

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

class HTTP3MethodsTest: public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    std::shared_ptr<quicx::IClient> client_;
    std::thread server_thread_;
    uint16_t port_ = 18443;

    static const char cert_pem_[];
    static const char key_pem_[];

    void SetUp() override {
        // Create and configure server
        server_ = quicx::IServer::Create();

        quicx::Http3ServerConfig server_config;
        server_config.cert_pem_ = cert_pem_;
        server_config.key_pem_ = key_pem_;
        server_config.config_.thread_num_ = 2;
        server_config.config_.log_level_ = quicx::LogLevel::kError;

        ASSERT_TRUE(server_->Init(server_config));

        RegisterHandlers();

        // Start server in background
        server_thread_ = std::thread([this]() { server_->Start("127.0.0.1", port_); });

        // Wait for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Create and configure client
        client_ = quicx::IClient::Create();

        quicx::Http3Config client_config;
        client_config.thread_num_ = 2;
        client_config.log_level_ = quicx::LogLevel::kError;
        client_config.connection_timeout_ms_ = 5000;

        ASSERT_TRUE(client_->Init(client_config));
    }

    void TearDown() override {
        server_->Stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    void RegisterHandlers() {
        // GET handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AppendBody("GET response");
            });

        // POST handler
        server_->AddHandler(quicx::HttpMethod::kPost, "/test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::string body = req->GetBodyAsString();
                resp->SetStatusCode(201);
                resp->AppendBody("POST received: " + body);
            });

        // PUT handler
        server_->AddHandler(quicx::HttpMethod::kPut, "/test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::string body = req->GetBodyAsString();
                resp->SetStatusCode(200);
                resp->AppendBody("PUT updated: " + body);
            });

        // DELETE handler
        server_->AddHandler(quicx::HttpMethod::kDelete, "/test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(204);
                resp->AppendBody("");
            });

        // HEAD handler
        server_->AddHandler(quicx::HttpMethod::kHead, "/test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AddHeader("Content-Length", "100");
            });
    }

    std::string MakeRequest(quicx::HttpMethod method, const std::string& body = "") {
        auto request = quicx::IRequest::Create();
        if (!body.empty()) {
            request->AppendBody(body);
        }

        std::atomic<bool> completed{false};
        std::string response_body;
        int status_code = 0;

        std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/test";

        client_->DoRequest(url, method, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            EXPECT_EQ(error, 0);
            if (error == 0) {
                status_code = response->GetStatusCode();
                response_body = response->GetBodyAsString();
            }
            completed = true;
        });

        // Wait for completion
        for (int i = 0; i < 100 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        EXPECT_TRUE(completed);
        return response_body;
    }
};

// Certificate definitions
const char HTTP3MethodsTest::cert_pem_[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
    "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
    "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
    "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
    "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
    "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
    "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
    "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
    "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
    "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
    "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
    "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
    "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
    "-----END CERTIFICATE-----\n";

const char HTTP3MethodsTest::key_pem_[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
    "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
    "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
    "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
    "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
    "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
    "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
    "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
    "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
    "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
    "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
    "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
    "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
    "-----END RSA PRIVATE KEY-----\n";

// Test Cases

TEST_F(HTTP3MethodsTest, GetRequest) {
    std::string response = MakeRequest(quicx::HttpMethod::kGet);
    EXPECT_EQ(response, "GET response");
}

TEST_F(HTTP3MethodsTest, PostRequest) {
    std::string response = MakeRequest(quicx::HttpMethod::kPost, "test data");
    EXPECT_EQ(response, "POST received: test data");
}

TEST_F(HTTP3MethodsTest, PutRequest) {
    std::string response = MakeRequest(quicx::HttpMethod::kPut, "updated data");
    EXPECT_EQ(response, "PUT updated: updated data");
}

TEST_F(HTTP3MethodsTest, DeleteRequest) {
    std::string response = MakeRequest(quicx::HttpMethod::kDelete);
    EXPECT_EQ(response, "");  // 204 No Content
}

TEST_F(HTTP3MethodsTest, HeadRequest) {
    std::string response = MakeRequest(quicx::HttpMethod::kHead);
    EXPECT_EQ(response, "");  // HEAD should have no body
}

TEST_F(HTTP3MethodsTest, LargePostBody) {
    std::string large_body(10 * 1024, 'X');  // 10KB
    std::string response = MakeRequest(quicx::HttpMethod::kPost, large_body);
    EXPECT_TRUE(response.find("POST received:") != std::string::npos);
}

TEST_F(HTTP3MethodsTest, ConcurrentRequests) {
    const int num_requests = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_requests; ++i) {
        threads.emplace_back([this, &success_count]() {
            std::string response = MakeRequest(quicx::HttpMethod::kGet);
            if (response == "GET response") {
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), num_requests);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
