#include <gtest/gtest.h>
#include "http3/http/request.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/response_stream.h"
#include "unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockClientConnection {
public:
    MockClientConnection(
        const std::shared_ptr<QpackEncoder>& qpack_encoder, std::shared_ptr<IQuicBidirectionStream> stream) {
        blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
        auto response_handler =
            std::bind(&MockClientConnection::ResponseHandler, this, std::placeholders::_1, std::placeholders::_2);
        auto error_handler =
            std::bind(&MockClientConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2);
        auto push_promise_handler = std::bind(&MockClientConnection::PushPromiseHandler, this, std::placeholders::_1);

        request_stream_ = std::make_shared<RequestStream>(
            qpack_encoder, blocked_registry_, stream, response_handler, error_handler, push_promise_handler);
    }
    ~MockClientConnection() {}

    bool SendRequest(std::shared_ptr<IRequest> request) { return request_stream_->SendRequest(request); }

    void ResponseHandler(std::shared_ptr<IResponse> response, uint32_t error) {
        response_ = response;
        error_code_ = error;
    }

    void PushPromiseHandler(std::unordered_map<std::string, std::string>& push_promise) {
        push_promise_ = push_promise;
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) { error_code_ = error_code; }

    const std::shared_ptr<IResponse>& GetResponse() { return response_; }
    const std::unordered_map<std::string, std::string>& GetPushPromise() { return push_promise_; }
    uint32_t GetErrorCode() { return error_code_; }

private:
    uint32_t error_code_;
    std::shared_ptr<IResponse> response_;
    std::unordered_map<std::string, std::string> push_promise_;
    std::shared_ptr<RequestStream> request_stream_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
};

class MockServerConnection {
public:
    MockServerConnection(
        const std::shared_ptr<QpackEncoder>& qpack_encoder, std::shared_ptr<IQuicBidirectionStream> stream):
        http_handler_(nullptr) {
        // Create a mock http processor
        class MockHttpProcessor: public IHttpProcessor {
        public:
            MockHttpProcessor(MockServerConnection* server):
                server_(server) {}
            RouteConfig MatchRoute(HttpMethod method, const std::string& path, std::shared_ptr<IRequest> request = nullptr) override {
                return server_->MatchHandler(method, path);
            }
            void BeforeHandlerProcess(std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) override {}
            void AfterHandlerProcess(std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) override {}

        private:
            MockServerConnection* server_;
        };

        auto processor = std::make_shared<MockHttpProcessor>(this);
        auto push_handler = [](std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>) {};

        blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
        response_stream_ =
            std::make_shared<ResponseStream>(qpack_encoder, blocked_registry_, stream, processor, push_handler,
                std::bind(&MockServerConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
                []() { return true; });  // Mock: always return true for settings_received
    }
    ~MockServerConnection() {}

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) { error_code_ = error_code; }

    RouteConfig MatchHandler(HttpMethod method, const std::string& path) {
        // Create wrapper that accesses http_handler_ at call time (not bind time)
        auto wrapper = [this](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
            if (this->http_handler_) {
                this->http_handler_(req, resp);
            } else {
                resp->SetStatusCode(200);
            }
        };
        return RouteConfig(wrapper);
    }

    void SetHttpHandler(http_handler http_handler) { http_handler_ = http_handler; }

    uint32_t GetErrorCode() { return error_code_; }

private:
    uint32_t error_code_;
    http_handler http_handler_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::shared_ptr<ResponseStream> response_stream_;
};

class RequestResponseStreamTest: public testing::Test {
protected:
    void SetUp() override {
        qpack_encoder_ = std::make_shared<QpackEncoder>();

        mock_stream_1_ = std::make_shared<quic::MockQuicStream>();
        mock_stream_2_ = std::make_shared<quic::MockQuicStream>();

        mock_stream_2_->SetPeer(mock_stream_1_);
        mock_stream_1_->SetPeer(mock_stream_2_);

        client_connection_ = std::make_shared<MockClientConnection>(qpack_encoder_, mock_stream_1_);
        server_connection_ = std::make_shared<MockServerConnection>(qpack_encoder_, mock_stream_2_);
    }

    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::MockQuicStream> mock_stream_1_;
    std::shared_ptr<quic::MockQuicStream> mock_stream_2_;
    std::shared_ptr<MockClientConnection> client_connection_;
    std::shared_ptr<MockServerConnection> server_connection_;
};

TEST_F(RequestResponseStreamTest, SendHeaders) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->AddHeader("Content-Type", "text/plain");
    request->SetMethod(HttpMethod::kGet);
    request->SetPath("/api/data");
    request->SetScheme("http");
    request->SetAuthority("api.example.com");

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        std::string content_type;
        EXPECT_TRUE(request->GetHeader("Content-Type", content_type));
        EXPECT_EQ(content_type, "text/plain");

        response->AddHeader("Content-Type", "text/plain");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));

    // Ensure response is produced
    ASSERT_NE(client_connection_->GetResponse(), nullptr);

    std::string content_type;
    ASSERT_NE(client_connection_->GetResponse(), nullptr);
    EXPECT_TRUE(client_connection_->GetResponse()->GetHeader("Content-Type", content_type));
    EXPECT_EQ(content_type, "text/plain");

    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);
}

TEST_F(RequestResponseStreamTest, SendHeadersAndBody) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->AddHeader("Content-Type", "text/plain");
    request->AppendBody("Hello, Server!");
    request->SetMethod(HttpMethod::kPost);
    request->SetPath("/api/data");
    request->SetScheme("http");
    request->SetAuthority("api.example.com");

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        std::string content_type;
        EXPECT_TRUE(request->GetHeader("Content-Type", content_type));
        EXPECT_EQ(content_type, "text/plain");
        EXPECT_EQ(request->GetBodyAsString(), "Hello, Server!");
        EXPECT_EQ(request->GetMethod(), HttpMethod::kPost);
        EXPECT_EQ(request->GetPath(), "/api/data");
        EXPECT_EQ(request->GetScheme(), "http");
        EXPECT_EQ(request->GetAuthority(), "api.example.com");

        response->SetStatusCode(400);
        response->AddHeader("Content-Type", "text/plain");
        response->AppendBody("Hello, Client!");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));

    // Give the mock a chance to deliver read callbacks synchronously
    // (no-op here, but placeholder if later made asynchronous)
    std::string content_type;
    ASSERT_NE(client_connection_->GetResponse(), nullptr);
    EXPECT_TRUE(client_connection_->GetResponse()->GetHeader("Content-Type", content_type));
    EXPECT_EQ(content_type, "text/plain");
    EXPECT_EQ(client_connection_->GetResponse()->GetBodyAsString(), "Hello, Client!");
    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 400);
}

TEST_F(RequestResponseStreamTest, SendBody) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->AppendBody("Hello, Server!");
    request->SetMethod(HttpMethod::kPost);

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        EXPECT_EQ(request->GetBodyAsString(), "Hello, Server!");
        response->AppendBody("Hello, Client!");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));
    ASSERT_NE(client_connection_->GetResponse(), nullptr);
    EXPECT_EQ(client_connection_->GetResponse()->GetBodyAsString(), "Hello, Client!");
    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);
}

// ============================================================================
// Buffer-based data sending tests (using existing test infrastructure)
// ============================================================================

TEST_F(RequestResponseStreamTest, SendLargeBodyUsingBuffer) {
    // Test sending a large body (10KB) using buffer operations
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->SetMethod(HttpMethod::kPost);
    request->SetPath("/upload");
    request->SetScheme("https");
    request->SetAuthority("example.com");

    // Create a 10KB body
    std::string body_content(10240, 'X');
    for (size_t i = 0; i < body_content.size(); i++) {
        body_content[i] = static_cast<char>('A' + (i % 26));
    }
    request->AppendBody(reinterpret_cast<const uint8_t*>(body_content.data()), body_content.size());

    auto http_handler = [&body_content](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        // Verify the received body matches what was sent
        std::string received_body = request->GetBodyAsString();
        EXPECT_EQ(body_content.size(), received_body.size());
        EXPECT_EQ(body_content, received_body);

        response->SetStatusCode(200);
        response->AppendBody("Upload successful");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));
    ASSERT_NE(client_connection_->GetResponse(), nullptr);
    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);
    EXPECT_EQ(client_connection_->GetResponse()->GetBodyAsString(), "Upload successful");
}

TEST_F(RequestResponseStreamTest, MultipleChunksAppend) {
    // Test appending body in multiple small chunks
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->SetMethod(HttpMethod::kPost);
    request->SetPath("/data");
    request->SetScheme("https");
    request->SetAuthority("example.com");

    // Append body in multiple small chunks
    request->AppendBody("chunk1");
    request->AppendBody("chunk2");
    request->AppendBody("chunk3");

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        EXPECT_EQ(request->GetBodyAsString(), "chunk1chunk2chunk3");
        response->SetStatusCode(200);

        // Response also with multiple chunks
        response->AppendBody("result1");
        response->AppendBody("result2");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));
    ASSERT_NE(client_connection_->GetResponse(), nullptr);
    EXPECT_EQ(client_connection_->GetResponse()->GetBodyAsString(), "result1result2");
}

TEST_F(RequestResponseStreamTest, BinaryDataBuffer) {
    // Test sending binary data using buffer
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->SetMethod(HttpMethod::kPost);
    request->SetPath("/binary");
    request->SetScheme("https");
    request->SetAuthority("example.com");

    // Create binary data with all byte values
    std::vector<uint8_t> binary_data(256);
    for (size_t i = 0; i < 256; i++) {
        binary_data[i] = static_cast<uint8_t>(i);
    }
    request->AppendBody(binary_data.data(), binary_data.size());

    auto http_handler = [&binary_data](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        auto body = request->GetBody();
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->GetDataLength(), binary_data.size());

        // Verify binary data integrity
        std::vector<uint8_t> received_data(binary_data.size());
        body->Read(received_data.data(), received_data.size());
        EXPECT_EQ(binary_data, received_data);

        response->SetStatusCode(200);
        response->AppendBody(binary_data.data(), binary_data.size());
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));
    ASSERT_NE(client_connection_->GetResponse(), nullptr);
    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);

    auto response_body = client_connection_->GetResponse()->GetBody();
    ASSERT_NE(response_body, nullptr);
    EXPECT_EQ(response_body->GetDataLength(), binary_data.size());
}

}  // namespace
}  // namespace http3
}  // namespace quicx