#include <gtest/gtest.h>
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "mock_quic_recv_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/response_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockClientConnection {
public:
    MockClientConnection(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream) {
        request_stream_ = std::make_shared<RequestStream>(qpack_encoder, stream,
            std::bind(&MockClientConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockClientConnection::ResponseHandler, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockClientConnection::PushPromiseHandler, this, std::placeholders::_1));
    }
    ~MockClientConnection() {}

    bool SendRequest(std::shared_ptr<IRequest> request) {
        return request_stream_->SendRequest(request);
    }

    void ResponseHandler(std::shared_ptr<IResponse> response, uint32_t error) {
        response_ = response;
        error_code_ = error;
    }

    void PushPromiseHandler(std::unordered_map<std::string, std::string>& push_promise) {
        push_promise_ = push_promise;
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    const std::shared_ptr<IResponse>& GetResponse() { return response_; }
    const std::unordered_map<std::string, std::string>& GetPushPromise() { return push_promise_; }
    uint32_t GetErrorCode() { return error_code_; }
private:
    uint32_t error_code_;
    std::shared_ptr<IResponse> response_;
    std::unordered_map<std::string, std::string> push_promise_;
    std::shared_ptr<RequestStream> request_stream_;
};

class MockServerConnection {
public:
    MockServerConnection(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        std::shared_ptr<quic::IQuicBidirectionStream> stream) {
        response_stream_ = std::make_shared<ResponseStream>(qpack_encoder, stream,
            std::bind(&MockServerConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockServerConnection::HttpHandler, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockServerConnection() {}

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    void HttpHandler(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        http_handler_(request, response);
    }

    void SetHttpHandler(http_handler http_handler) {
        http_handler_ = http_handler;
    }

    uint32_t GetErrorCode() { return error_code_; }
private:
    uint32_t error_code_;
    http_handler http_handler_;
    std::shared_ptr<ResponseStream> response_stream_;
};

class RequestResponseStreamTest
    : public testing::Test {
protected:
    void SetUp() override {
        qpack_encoder_ = std::make_shared<QpackEncoder>();
        
        mock_stream_1_ = std::make_shared<quic::MockQuicRecvStream>();
        mock_stream_2_ = std::make_shared<quic::MockQuicRecvStream>();

        mock_stream_2_->SetPeer(mock_stream_1_);
        mock_stream_1_->SetPeer(mock_stream_2_);

        client_connection_ = std::make_shared<MockClientConnection>(qpack_encoder_, mock_stream_1_);
        server_connection_ = std::make_shared<MockServerConnection>(qpack_encoder_, mock_stream_2_);

    }

    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::MockQuicRecvStream> mock_stream_1_;
    std::shared_ptr<quic::MockQuicRecvStream> mock_stream_2_;
    std::shared_ptr<MockClientConnection> client_connection_;
    std::shared_ptr<MockServerConnection> server_connection_;
};

TEST_F(RequestResponseStreamTest, SendHeaders) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->AddHeader("Content-Type", "text/plain");
    request->SetMethod(HttpMothed::HM_GET);
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

    std::string content_type;
    EXPECT_TRUE(client_connection_->GetResponse()->GetHeader("Content-Type", content_type));
    EXPECT_EQ(content_type, "text/plain");

    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);
}

TEST_F(RequestResponseStreamTest, SendHeadersAndBody) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->AddHeader("Content-Type", "text/plain");
    request->SetBody("Hello, Server!");
    request->SetMethod(HttpMothed::HM_POST);
    request->SetPath("/api/data");
    request->SetScheme("http");
    request->SetAuthority("api.example.com");

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        std::string content_type;
        EXPECT_TRUE(request->GetHeader("Content-Type", content_type));
        EXPECT_EQ(content_type, "text/plain");
        EXPECT_EQ(request->GetBody(), "Hello, Server!");
        EXPECT_EQ(request->GetMethod(), HttpMothed::HM_POST);
        EXPECT_EQ(request->GetPath(), "/api/data");
        EXPECT_EQ(request->GetScheme(), "http");
        EXPECT_EQ(request->GetAuthority(), "api.example.com");

        response->AddHeader("Content-Type", "text/plain");
        response->SetBody("Hello, Client!");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));

    std::string content_type;
    EXPECT_TRUE(client_connection_->GetResponse()->GetHeader("Content-Type", content_type));
    EXPECT_EQ(content_type, "text/plain");
    EXPECT_EQ(client_connection_->GetResponse()->GetBody(), "Hello, Client!");
}

TEST_F(RequestResponseStreamTest, SendBody) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->SetBody("Hello, Server!");
    request->SetMethod(HttpMothed::HM_POST);

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        EXPECT_EQ(request->GetBody(), "Hello, Server!");
        response->SetBody("Hello, Client!");
    };
    server_connection_->SetHttpHandler(http_handler);

    EXPECT_TRUE(client_connection_->SendRequest(request));
    EXPECT_EQ(client_connection_->GetResponse()->GetBody(), "Hello, Client!");
    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);
}

}  // namespace
}  // namespace http3
}  // namespace quicx