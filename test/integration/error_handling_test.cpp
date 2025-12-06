// Error Handling Integration Test
// Tests timeout, protocol errors, and error recovery

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

class ErrorHandlingTest: public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    std::shared_ptr<quicx::IClient> client_;
    std::thread server_thread_;
    uint16_t port_ = 18445;

    static const char cert_pem_[];
    static const char key_pem_[];

    void SetUp() override {
        server_ = quicx::IServer::Create();

        quicx::Http3ServerConfig server_config;
        server_config.cert_pem_ = cert_pem_;
        server_config.key_pem_ = key_pem_;
        server_config.config_.thread_num_ = 2;
        server_config.config_.log_level_ = quicx::LogLevel::kError;

        ASSERT_TRUE(server_->Init(server_config));

        // Normal handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/ok",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AppendBody("OK");
            });

        // Error handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/error",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(500);
                resp->AppendBody("Internal Server Error");
            });

        // Timeout handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/timeout",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                resp->SetStatusCode(200);
                resp->AppendBody("Delayed");
            });

        server_thread_ = std::thread([this]() { server_->Start("127.0.0.1", port_); });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Create client
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
};

const char ErrorHandlingTest::cert_pem_[] =
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

const char ErrorHandlingTest::key_pem_[] =
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

TEST_F(ErrorHandlingTest, ServerError) {
    auto request = quicx::IRequest::Create();
    std::atomic<bool> completed{false};
    int status_code = 0;

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/error";

    client_->DoRequest(
        url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            EXPECT_EQ(error, 0);  // Request should succeed
            if (error == 0) {
                status_code = response->GetStatusCode();
            }
            completed = true;
        });

    for (int i = 0; i < 100 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(completed);
    EXPECT_EQ(status_code, 500);
}

TEST_F(ErrorHandlingTest, RequestTimeout) {
    auto request = quicx::IRequest::Create();
    std::atomic<bool> completed{false};
    uint32_t error_code = 0;

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/timeout";

    auto start = std::chrono::steady_clock::now();

    client_->DoRequest(
        url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            error_code = error;
            completed = true;
        });

    for (int i = 0; i < 200 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    EXPECT_TRUE(completed);
    // Should either timeout or take long time
    EXPECT_TRUE(duration.count() >= 5 || error_code != 0);
}

TEST_F(ErrorHandlingTest, InvalidURL) {
    auto request = quicx::IRequest::Create();
    std::atomic<bool> completed{false};
    uint32_t error_code = 0;

    // Invalid port
    std::string url = "https://127.0.0.1:99999/test";

    client_->DoRequest(
        url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            error_code = error;
            completed = true;
        });

    for (int i = 0; i < 100 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(completed);
    EXPECT_NE(error_code, 0);  // Should fail
}

TEST_F(ErrorHandlingTest, RecoveryAfterError) {
    // First request - error
    {
        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/error";

        client_->DoRequest(url, quicx::HttpMethod::kGet, request,
            [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) { completed = true; });

        for (int i = 0; i < 100 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        EXPECT_TRUE(completed);
    }

    // Second request - should succeed
    {
        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};
        int status_code = 0;

        std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/ok";

        client_->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error == 0) {
                    status_code = response->GetStatusCode();
                }
                completed = true;
            });

        for (int i = 0; i < 100 && !completed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        EXPECT_TRUE(completed);
        EXPECT_EQ(status_code, 200);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
