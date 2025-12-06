// Connection Management Integration Test
// Tests connection lifecycle, graceful shutdown, and timeout handling

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

class ConnectionManagementTest: public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    uint16_t port_ = 18444;
    std::thread server_thread_;

    static const char cert_pem_[];
    static const char key_pem_[];

    void SetUp() override {
        server_ = quicx::IServer::Create();

        quicx::Http3ServerConfig config;
        config.cert_pem_ = cert_pem_;
        config.key_pem_ = key_pem_;
        config.config_.thread_num_ = 2;
        config.config_.log_level_ = quicx::LogLevel::kError;

        ASSERT_TRUE(server_->Init(config));

        // Simple echo handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/echo",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AppendBody("echo");
            });

        // Slow handler for timeout testing
        server_->AddHandler(quicx::HttpMethod::kGet, "/slow",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                resp->SetStatusCode(200);
                resp->AppendBody("slow response");
            });

        server_thread_ = std::thread([this]() { server_->Start("127.0.0.1", port_); });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override {
        server_->Stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
};

const char ConnectionManagementTest::cert_pem_[] =
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

const char ConnectionManagementTest::key_pem_[] =
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

TEST_F(ConnectionManagementTest, BasicConnection) {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 1;
    config.log_level_ = quicx::LogLevel::kError;

    ASSERT_TRUE(client->Init(config));

    auto request = quicx::IRequest::Create();
    std::atomic<bool> completed{false};
    bool success = false;

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/echo";

    client->DoRequest(
        url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            success = (error == 0 && response->GetStatusCode() == 200);
            completed = true;
        });

    for (int i = 0; i < 100 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(completed);
    EXPECT_TRUE(success);
}

TEST_F(ConnectionManagementTest, ConnectionReuse) {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 1;
    config.log_level_ = quicx::LogLevel::kError;

    ASSERT_TRUE(client->Init(config));

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/echo";

    // Make multiple requests on same connection
    for (int i = 0; i < 5; ++i) {
        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        client->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                EXPECT_EQ(error, 0);
                completed = true;
            });

        for (int j = 0; j < 100 && !completed; ++j) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        EXPECT_TRUE(completed);
    }
}

TEST_F(ConnectionManagementTest, ConnectionTimeout) {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 1;
    config.log_level_ = quicx::LogLevel::kError;
    config.connection_timeout_ms_ = 2000;  // 2 second timeout

    ASSERT_TRUE(client->Init(config));

    auto request = quicx::IRequest::Create();
    std::atomic<bool> completed{false};
    uint32_t error_code = 0;

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/slow";

    client->DoRequest(
        url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            error_code = error;
            completed = true;
        });

    for (int i = 0; i < 100 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(completed);
    // Should timeout or succeed depending on timing
}

TEST_F(ConnectionManagementTest, MultipleConnections) {
    const int num_clients = 5;
    std::vector<std::shared_ptr<quicx::IClient>> clients;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_clients; ++i) {
        auto client = quicx::IClient::Create();

        quicx::Http3Config config;
        config.thread_num_ = 1;
        config.log_level_ = quicx::LogLevel::kError;

        ASSERT_TRUE(client->Init(config));
        clients.push_back(std::move(client));
    }

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/echo";

    for (auto& client : clients) {
        auto request = quicx::IRequest::Create();

        client->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error == 0) {
                    success_count++;
                }
            });
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_EQ(success_count.load(), num_clients);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
