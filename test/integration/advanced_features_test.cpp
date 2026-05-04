// Advanced Features Integration Test
// Tests path parameters, custom headers, query parameters, middleware, and body provider

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

class AdvancedFeaturesTest : public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    std::shared_ptr<quicx::IClient> client_;
    std::thread server_thread_;
    uint16_t port_;
    static std::atomic<uint16_t> next_port_;

    static const char cert_pem_[];
    static const char key_pem_[];

    void SetUp() override {
        port_ = next_port_.fetch_add(1);

        server_ = quicx::IServer::Create();

        quicx::Http3ServerConfig server_config;
        server_config.quic_config_.cert_pem_ = cert_pem_;
        server_config.quic_config_.key_pem_ = key_pem_;
        server_config.quic_config_.config_.worker_thread_num_ = 2;
        server_config.quic_config_.config_.log_level_ = quicx::LogLevel::kError;

        ASSERT_TRUE(server_->Init(server_config));

        RegisterHandlers();

        server_thread_ = std::thread([this]() { server_->Start("127.0.0.1", port_); });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        client_ = quicx::IClient::Create();

        quicx::Http3ClientConfig client_config;
        client_config.quic_config_.verify_peer_ = false;
        client_config.quic_config_.config_.worker_thread_num_ = 2;
        client_config.quic_config_.config_.log_level_ = quicx::LogLevel::kError;
        client_config.connection_timeout_ms_ = 5000;

        ASSERT_TRUE(client_->Init(client_config));
    }

    void TearDown() override {
        if (client_) {
            client_->Close();
            client_.reset();
        }
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

    void RegisterHandlers() {
        // Path parameter handler: /users/:id
        server_->AddHandler(quicx::HttpMethod::kGet, "/users/:id",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                auto& params = req->GetPathParams();
                auto it = params.find("id");
                if (it != params.end()) {
                    resp->SetStatusCode(200);
                    resp->AppendBody("user_id=" + it->second);
                } else {
                    resp->SetStatusCode(400);
                    resp->AppendBody("missing id");
                }
            });

        // Nested path parameters: /users/:user_id/posts/:post_id
        server_->AddHandler(quicx::HttpMethod::kGet, "/users/:user_id/posts/:post_id",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                auto& params = req->GetPathParams();
                auto uid = params.find("user_id");
                auto pid = params.find("post_id");
                if (uid != params.end() && pid != params.end()) {
                    resp->SetStatusCode(200);
                    resp->AppendBody("user=" + uid->second + ",post=" + pid->second);
                } else {
                    resp->SetStatusCode(400);
                    resp->AppendBody("missing params");
                }
            });

        // Custom headers echo handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/echo-headers",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::string custom_val;
                if (req->GetHeader("x-custom-header", custom_val)) {
                    resp->AddHeader("x-echo-header", custom_val);
                }
                std::string auth_val;
                if (req->GetHeader("x-auth-token", auth_val)) {
                    resp->AddHeader("x-echo-auth", auth_val);
                }
                resp->SetStatusCode(200);
                resp->AppendBody("headers echoed");
            });

        // Query parameter handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/search",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                auto& query = req->GetQueryParams();
                std::string result;
                auto q_it = query.find("q");
                if (q_it != query.end()) {
                    result += "q=" + q_it->second;
                }
                auto page_it = query.find("page");
                if (page_it != query.end()) {
                    if (!result.empty()) result += "&";
                    result += "page=" + page_it->second;
                }
                if (result.empty()) {
                    result = "no params";
                }
                resp->SetStatusCode(200);
                resp->AppendBody(result);
            });

        // Body provider handler: streams response in chunks
        server_->AddHandler(quicx::HttpMethod::kGet, "/stream-response",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AddHeader("content-type", "application/octet-stream");

                // Create a body provider that produces 4KB of data in chunks
                auto state = std::make_shared<size_t>(0);
                const size_t total_size = 4096;

                resp->SetResponseBodyProvider([state, total_size](uint8_t* buffer, size_t buffer_size) -> size_t {
                    if (*state >= total_size) {
                        return 0;  // End of body
                    }
                    size_t remaining = total_size - *state;
                    size_t to_write = std::min(remaining, buffer_size);
                    // Fill with repeating pattern
                    for (size_t i = 0; i < to_write; ++i) {
                        buffer[i] = static_cast<uint8_t>((*state + i) % 256);
                    }
                    *state += to_write;
                    return to_write;
                });
            });

        // Multiple response headers
        server_->AddHandler(quicx::HttpMethod::kGet, "/multi-headers",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AddHeader("x-request-id", "req-12345");
                resp->AddHeader("x-rate-limit", "100");
                resp->AddHeader("content-type", "application/json");
                resp->AppendBody("{\"status\":\"ok\"}");
            });

        // Middleware test handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/middleware-test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                // Check if middleware added a header
                std::string middleware_val;
                if (req->GetHeader("x-before-middleware", middleware_val)) {
                    resp->AppendBody("middleware=" + middleware_val);
                } else {
                    resp->AppendBody("no middleware");
                }
            });

        // Register a before-middleware that adds a header to all GET requests
        server_->AddMiddleware(quicx::HttpMethod::kGet, quicx::MiddlewarePosition::kBefore,
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                req->AddHeader("x-before-middleware", "applied");
            });

        // Register an after-middleware that adds a response header to all GET requests
        server_->AddMiddleware(quicx::HttpMethod::kGet, quicx::MiddlewarePosition::kAfter,
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->AddHeader("x-after-middleware", "done");
            });

        // 404 test: no handler for /nonexistent
    }

    struct RequestResult {
        int status_code = 0;
        std::string body;
        std::unordered_map<std::string, std::string> headers;
        uint32_t error = 0;
        bool completed = false;
    };

    RequestResult DoRequest(const std::string& path, quicx::HttpMethod method = quicx::HttpMethod::kGet,
        const std::string& body = "",
        const std::unordered_map<std::string, std::string>& headers = {}) {
        auto request = quicx::IRequest::Create();
        if (!body.empty()) {
            request->AppendBody(body);
        }
        for (auto& [k, v] : headers) {
            request->AddHeader(k, v);
        }

        RequestResult result;
        std::atomic<bool> done{false};

        std::string url = "https://127.0.0.1:" + std::to_string(port_) + path;

        client_->DoRequest(url, method, request,
            [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                result.error = error;
                if (error == 0 && response) {
                    result.status_code = response->GetStatusCode();
                    result.body = response->GetBodyAsString();
                    result.headers = response->GetHeaders();
                }
                result.completed = true;
                done = true;
            });

        for (int i = 0; i < 100 && !done; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        return result;
    }
};

std::atomic<uint16_t> AdvancedFeaturesTest::next_port_(18580);

const char AdvancedFeaturesTest::cert_pem_[] =
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

const char AdvancedFeaturesTest::key_pem_[] =
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

// ==================== Path Parameter Tests ====================

TEST_F(AdvancedFeaturesTest, SinglePathParam) {
    auto result = DoRequest("/users/42");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "user_id=42");
}

TEST_F(AdvancedFeaturesTest, PathParamWithString) {
    auto result = DoRequest("/users/john");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "user_id=john");
}

TEST_F(AdvancedFeaturesTest, NestedPathParams) {
    auto result = DoRequest("/users/5/posts/100");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "user=5,post=100");
}

// ==================== Custom Header Tests ====================

TEST_F(AdvancedFeaturesTest, CustomRequestHeaders) {
    auto result = DoRequest("/echo-headers", quicx::HttpMethod::kGet, "",
        {{"x-custom-header", "hello-world"}, {"x-auth-token", "secret123"}});
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "headers echoed");

    // Verify echoed headers
    auto it = result.headers.find("x-echo-header");
    EXPECT_NE(it, result.headers.end());
    if (it != result.headers.end()) {
        EXPECT_EQ(it->second, "hello-world");
    }

    auto auth_it = result.headers.find("x-echo-auth");
    EXPECT_NE(auth_it, result.headers.end());
    if (auth_it != result.headers.end()) {
        EXPECT_EQ(auth_it->second, "secret123");
    }
}

TEST_F(AdvancedFeaturesTest, MultipleResponseHeaders) {
    auto result = DoRequest("/multi-headers");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "{\"status\":\"ok\"}");

    auto req_id = result.headers.find("x-request-id");
    EXPECT_NE(req_id, result.headers.end());
    if (req_id != result.headers.end()) {
        EXPECT_EQ(req_id->second, "req-12345");
    }

    auto rate_limit = result.headers.find("x-rate-limit");
    EXPECT_NE(rate_limit, result.headers.end());
    if (rate_limit != result.headers.end()) {
        EXPECT_EQ(rate_limit->second, "100");
    }
}

// ==================== Query Parameter Tests ====================

TEST_F(AdvancedFeaturesTest, QueryParams) {
    auto result = DoRequest("/search?q=hello&page=2");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    // Query params order might vary, check both
    EXPECT_TRUE(result.body.find("q=hello") != std::string::npos);
    EXPECT_TRUE(result.body.find("page=2") != std::string::npos);
}

TEST_F(AdvancedFeaturesTest, QueryParamsSingleKey) {
    auto result = DoRequest("/search?q=test");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "q=test");
}

TEST_F(AdvancedFeaturesTest, NoQueryParams) {
    auto result = DoRequest("/search");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "no params");
}

// ==================== Body Provider (Streaming Response) Tests ====================

TEST_F(AdvancedFeaturesTest, StreamingResponseBodyProvider) {
    auto result = DoRequest("/stream-response");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    // Verify total size
    EXPECT_EQ(result.body.size(), 4096u);

    // Verify data pattern
    bool pattern_ok = true;
    for (size_t i = 0; i < result.body.size(); ++i) {
        if (static_cast<uint8_t>(result.body[i]) != static_cast<uint8_t>(i % 256)) {
            pattern_ok = false;
            break;
        }
    }
    EXPECT_TRUE(pattern_ok);
}

// ==================== Middleware Tests ====================

TEST_F(AdvancedFeaturesTest, BeforeMiddleware) {
    auto result = DoRequest("/middleware-test");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.status_code, 200);
    EXPECT_EQ(result.body, "middleware=applied");
}

TEST_F(AdvancedFeaturesTest, AfterMiddleware) {
    auto result = DoRequest("/middleware-test");
    EXPECT_TRUE(result.completed);

    auto it = result.headers.find("x-after-middleware");
    EXPECT_NE(it, result.headers.end());
    if (it != result.headers.end()) {
        EXPECT_EQ(it->second, "done");
    }
}

// ==================== 404 Not Found Tests ====================

TEST_F(AdvancedFeaturesTest, NotFoundRoute) {
    auto result = DoRequest("/nonexistent-path");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.error, 0u);
    EXPECT_EQ(result.status_code, 404);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
