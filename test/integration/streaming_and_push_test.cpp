// Streaming and Push Integration Test
// Tests IAsyncServerHandler (streaming upload), IAsyncClientHandler (streaming download),
// request body provider (streaming upload from client), and Server Push

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "http3/include/if_async_handler.h"
#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

// ==================== Async Server Handler for streaming upload ====================

class StreamingUploadHandler : public quicx::IAsyncServerHandler {
public:
    void OnHeaders(std::shared_ptr<quicx::IRequest> request, std::shared_ptr<quicx::IResponse> response) override {
        response_ = response;
        received_data_.clear();

        // Extract path param
        auto& params = request->GetPathParams();
        auto it = params.find("name");
        if (it != params.end()) {
            upload_name_ = it->second;
        }
    }

    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
        if (data && length > 0) {
            received_data_.append(reinterpret_cast<const char*>(data), length);
        }
        if (is_last && response_) {
            response_->SetStatusCode(200);
            response_->AddHeader("content-type", "application/json");
            std::string body = "{\"name\":\"" + upload_name_ + "\",\"size\":" + std::to_string(received_data_.size()) + "}";
            response_->AppendBody(body);
        }
    }

    void OnError(uint32_t error_code) override {
        // Protocol error
    }

private:
    std::shared_ptr<quicx::IResponse> response_;
    std::string received_data_;
    std::string upload_name_;
};

// ==================== Test Fixture ====================

class StreamingAndPushTest : public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    std::thread server_thread_;
    uint16_t port_;
    static std::atomic<uint16_t> next_port_;

    static const char cert_pem_[];
    static const char key_pem_[];

    void SetUp() override {
        port_ = next_port_.fetch_add(1);
    }

    void TearDown() override {
        if (server_) {
            server_->Stop();
            server_->Join();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        server_.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void StartServer(bool enable_push = false) {
        quicx::Http3Settings settings = quicx::kDefaultHttp3Settings;
        server_ = quicx::IServer::Create(settings);

        quicx::Http3ServerConfig server_config;
        server_config.quic_config_.cert_pem_ = cert_pem_;
        server_config.quic_config_.key_pem_ = key_pem_;
        server_config.quic_config_.config_.worker_thread_num_ = 2;
        server_config.quic_config_.config_.log_level_ = quicx::LogLevel::kError;
        server_config.enable_push_ = enable_push;

        ASSERT_TRUE(server_->Init(server_config));
    }

    std::shared_ptr<quicx::IClient> CreateClient(bool enable_push = false) {
        quicx::Http3Settings settings = quicx::kDefaultHttp3Settings;
        auto client = quicx::IClient::Create(settings);

        quicx::Http3ClientConfig config;
        config.quic_config_.verify_peer_ = false;
        config.quic_config_.config_.worker_thread_num_ = 2;
        config.quic_config_.config_.log_level_ = quicx::LogLevel::kError;
        config.connection_timeout_ms_ = 5000;
        config.enable_push_ = enable_push;

        if (!client->Init(config)) {
            return nullptr;
        }
        return client;
    }

    void StartServerThread() {
        server_thread_ = std::thread([this]() { server_->Start("127.0.0.1", port_); });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

std::atomic<uint16_t> StreamingAndPushTest::next_port_(18600);

const char StreamingAndPushTest::cert_pem_[] =
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

const char StreamingAndPushTest::key_pem_[] =
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

// ==================== Async Server Handler (Streaming Upload) Tests ====================

TEST_F(StreamingAndPushTest, AsyncServerHandlerUpload) {
    StartServer();

    // Register async handler for streaming upload
    server_->AddHandler(quicx::HttpMethod::kPost, "/upload/:name",
        std::make_shared<StreamingUploadHandler>());

    StartServerThread();

    auto client = CreateClient();
    ASSERT_NE(client, nullptr);

    // Send POST with body
    auto request = quicx::IRequest::Create();
    std::string upload_data(2048, 'A');  // 2KB of 'A'
    request->AppendBody(upload_data);

    std::atomic<bool> completed{false};
    int status_code = 0;
    std::string response_body;

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/upload/testfile";

    client->DoRequest(url, quicx::HttpMethod::kPost, request,
        [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            EXPECT_EQ(error, 0u);
            if (error == 0 && response) {
                status_code = response->GetStatusCode();
                response_body = response->GetBodyAsString();
            }
            completed = true;
        });

    for (int i = 0; i < 100 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(completed.load());
    EXPECT_EQ(status_code, 200);
    EXPECT_TRUE(response_body.find("\"name\":\"testfile\"") != std::string::npos);
    EXPECT_TRUE(response_body.find("\"size\":2048") != std::string::npos);

    client->Close();
}

// ==================== Request Body Provider (Client Streaming) Tests ====================

TEST_F(StreamingAndPushTest, ClientBodyProvider) {
    StartServer();

    // Server handler that echoes body size
    server_->AddHandler(quicx::HttpMethod::kPost, "/echo-size",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            std::string body = req->GetBodyAsString();
            resp->SetStatusCode(200);
            resp->AppendBody("size=" + std::to_string(body.size()));
        });

    StartServerThread();

    auto client = CreateClient();
    ASSERT_NE(client, nullptr);

    // Use body provider to stream data
    auto request = quicx::IRequest::Create();
    const size_t total_size = 8192;  // 8KB
    auto bytes_sent = std::make_shared<size_t>(0);

    request->SetRequestBodyProvider([bytes_sent, total_size](uint8_t* buffer, size_t buffer_size) -> size_t {
        if (*bytes_sent >= total_size) {
            return 0;  // Done
        }
        size_t remaining = total_size - *bytes_sent;
        size_t to_write = std::min(remaining, buffer_size);
        std::memset(buffer, 'B', to_write);
        *bytes_sent += to_write;
        return to_write;
    });

    // Manually set content-length for complete mode server handler
    request->AddHeader("content-length", std::to_string(total_size));

    std::atomic<bool> completed{false};
    std::string response_body;
    int status_code = 0;

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/echo-size";

    client->DoRequest(url, quicx::HttpMethod::kPost, request,
        [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            EXPECT_EQ(error, 0u);
            if (error == 0 && response) {
                status_code = response->GetStatusCode();
                response_body = response->GetBodyAsString();
            }
            completed = true;
        });

    for (int i = 0; i < 100 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(completed.load());
    EXPECT_EQ(status_code, 200);
    EXPECT_EQ(response_body, "size=8192");

    client->Close();
}

// ==================== Async Client Handler (Streaming Download) Tests ====================

class TestAsyncClientHandler : public quicx::IAsyncClientHandler {
public:
    void OnHeaders(std::shared_ptr<quicx::IResponse> response) override {
        status_code = response->GetStatusCode();
        headers_received = true;
    }

    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
        if (data && length > 0) {
            received_data.append(reinterpret_cast<const char*>(data), length);
        }
        chunk_count++;
        if (is_last) {
            completed = true;
        }
    }

    void OnError(uint32_t error_code) override {
        error = error_code;
        completed = true;
    }

    std::atomic<bool> headers_received{false};
    std::atomic<bool> completed{false};
    std::atomic<int> chunk_count{0};
    int status_code = 0;
    uint32_t error = 0;
    std::string received_data;
};

TEST_F(StreamingAndPushTest, AsyncClientHandlerDownload) {
    StartServer();

    // Server handler with body provider that streams 4KB
    server_->AddHandler(quicx::HttpMethod::kGet, "/download",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            resp->SetStatusCode(200);
            auto sent = std::make_shared<size_t>(0);
            const size_t total = 4096;
            resp->SetResponseBodyProvider([sent, total](uint8_t* buf, size_t buf_size) -> size_t {
                if (*sent >= total) return 0;
                size_t remaining = total - *sent;
                size_t to_write = std::min(remaining, buf_size);
                for (size_t i = 0; i < to_write; ++i) {
                    buf[i] = static_cast<uint8_t>((*sent + i) % 256);
                }
                *sent += to_write;
                return to_write;
            });
        });

    StartServerThread();

    auto client = CreateClient();
    ASSERT_NE(client, nullptr);

    auto handler = std::make_shared<TestAsyncClientHandler>();

    auto request = quicx::IRequest::Create();
    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/download";

    client->DoRequest(url, quicx::HttpMethod::kGet, request, handler);

    for (int i = 0; i < 200 && !handler->completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(handler->completed.load());
    EXPECT_TRUE(handler->headers_received.load());
    EXPECT_EQ(handler->status_code, 200);
    EXPECT_EQ(handler->error, 0u);
    EXPECT_EQ(handler->received_data.size(), 4096u);

    // Verify data pattern
    bool pattern_ok = true;
    for (size_t i = 0; i < handler->received_data.size(); ++i) {
        if (static_cast<uint8_t>(handler->received_data[i]) != static_cast<uint8_t>(i % 256)) {
            pattern_ok = false;
            break;
        }
    }
    EXPECT_TRUE(pattern_ok);

    client->Close();
}

// ==================== Server Push Tests ====================

TEST_F(StreamingAndPushTest, ServerPushAccepted) {
    StartServer(true);  // Enable push

    server_->AddHandler(quicx::HttpMethod::kGet, "/with-push",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody("main response");

            // Create push response
            auto push_resp = quicx::IResponse::Create();
            push_resp->AddHeader("x-push-type", "preload");
            push_resp->SetStatusCode(200);
            push_resp->AppendBody("pushed content");
            resp->AppendPush(push_resp);
        });

    StartServerThread();

    auto client = CreateClient(true);  // Enable push
    ASSERT_NE(client, nullptr);

    std::atomic<bool> main_completed{false};
    std::atomic<bool> push_promise_received{false};
    std::atomic<bool> push_response_received{false};
    std::string main_body;
    std::string push_body;

    // Set push promise handler
    client->SetPushPromiseHandler([&](std::unordered_map<std::string, std::string>& headers) -> bool {
        push_promise_received = true;
        return true;  // Accept push
    });

    // Set push handler
    client->SetPushHandler([&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
        if (error == 0 && response) {
            push_body = response->GetBodyAsString();
        }
        push_response_received = true;
    });

    auto request = quicx::IRequest::Create();
    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/with-push";

    client->DoRequest(url, quicx::HttpMethod::kGet, request,
        [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0 && response) {
                main_body = response->GetBodyAsString();
            }
            main_completed = true;
        });

    // Wait for main response
    for (int i = 0; i < 100 && !main_completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(main_completed.load());
    EXPECT_EQ(main_body, "main response");

    // Wait a bit more for push to arrive
    for (int i = 0; i < 100 && !push_response_received; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Push should have been received
    EXPECT_TRUE(push_promise_received.load());
    EXPECT_TRUE(push_response_received.load());
    EXPECT_EQ(push_body, "pushed content");

    client->Close();
}

TEST_F(StreamingAndPushTest, ServerPushCancelled) {
    StartServer(true);

    server_->AddHandler(quicx::HttpMethod::kGet, "/with-push-cancel",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            resp->SetStatusCode(200);
            resp->AppendBody("main only");

            auto push_resp = quicx::IResponse::Create();
            push_resp->SetStatusCode(200);
            push_resp->AppendBody("should not arrive");
            resp->AppendPush(push_resp);
        });

    StartServerThread();

    auto client = CreateClient(true);
    ASSERT_NE(client, nullptr);

    std::atomic<bool> main_completed{false};
    std::atomic<bool> push_promise_received{false};
    std::string main_body;

    // Set push promise handler that REJECTS push
    client->SetPushPromiseHandler([&](std::unordered_map<std::string, std::string>& headers) -> bool {
        push_promise_received = true;
        return false;  // Reject push
    });

    auto request = quicx::IRequest::Create();
    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/with-push-cancel";

    client->DoRequest(url, quicx::HttpMethod::kGet, request,
        [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
            if (error == 0 && response) {
                main_body = response->GetBodyAsString();
            }
            main_completed = true;
        });

    for (int i = 0; i < 100 && !main_completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(main_completed.load());
    EXPECT_EQ(main_body, "main only");
    // Push promise should have been received but rejected
    EXPECT_TRUE(push_promise_received.load());

    client->Close();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
