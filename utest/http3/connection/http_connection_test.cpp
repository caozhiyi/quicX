#include <gtest/gtest.h>
#include "http3/include/type.h"
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/connection/client_connection.h"
#include "http3/connection/server_connection.h"
#include "utest/http3/connection/mock_quic_connection.h"

namespace quicx {
namespace http3 {
namespace {

class MockClient {
public:
    MockClient(std::shared_ptr<quic::IQuicConnection> conn) {
        conn_ = std::make_shared<ClientConnection>("", conn,
            std::bind(&MockClient::ErrorHandler, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockClient::PushPromiseHandler, this, std::placeholders::_1),
            std::bind(&MockClient::PushHandler, this, std::placeholders::_1, std::placeholders::_2));
    }

    void ErrorHandler(const std::string& unique_id, uint32_t error_code) {
        // TODO: implement this
    }
    void PushPromiseHandler(std::unordered_map<std::string, std::string>& headers) {
        // TODO: implement this
    }
    void PushHandler(std::shared_ptr<IResponse> response, uint32_t error) {
        // TODO: implement this
    }
    // send request
    bool DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler) {
        return conn_->DoRequest(request, handler);
    }
    void SetMaxPushID(uint64_t max_push_id) {
        conn_->SetMaxPushID(max_push_id);
    }
    void CancelPush(uint64_t push_id) {
        conn_->CancelPush(push_id);
    }
private:
    std::shared_ptr<ClientConnection> conn_;
};

class MockServer {
public:
    MockServer(std::shared_ptr<quic::IQuicConnection> conn) {
        conn_ = std::make_shared<ServerConnection>("", conn,
            std::bind(&MockServer::ErrorHandler, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockServer::HttpHandler, this, std::placeholders::_1, std::placeholders::_2));
    }

    void ErrorHandler(const std::string& unique_id, uint32_t error_code) {
        // TODO: implement this
    }

    void HttpHandler(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        http_handler_(request, response);
    }

    void SetHttpHandler(const http_handler& handler) {
        http_handler_ = handler;
    }

private:
    std::shared_ptr<ServerConnection> conn_;
    http_handler http_handler_;
};

class HttpConnectionTest
    :public testing::Test {
protected:
    void SetUp() override {
        mock_conn_1_ = std::make_shared<quic::MockQuicConnection>();
        mock_conn_2_ = std::make_shared<quic::MockQuicConnection>();

        mock_conn_1_->SetPeer(mock_conn_2_);
        mock_conn_2_->SetPeer(mock_conn_1_);

        mock_client_ = std::make_shared<MockClient>(mock_conn_1_);
        mock_server_ = std::make_shared<MockServer>(mock_conn_2_);
    }

    std::shared_ptr<quic::MockQuicConnection> mock_conn_1_;
    std::shared_ptr<quic::MockQuicConnection> mock_conn_2_;
    std::shared_ptr<MockClient> mock_client_;
    std::shared_ptr<MockServer> mock_server_;
};


TEST_F(HttpConnectionTest, DoRequest) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->SetMethod(HttpMethod::HM_GET);
    request->SetPath("/");
    request->SetScheme("http");
    request->SetAuthority("localhost");
    request->AddHeader("User-Agent", "curl/7.64.1");
    request->AddHeader("Accept", "*/*");
    request->AddHeader("Content-Type", "text/plain");
    request->SetBody("Hello, Server!");
    
    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        // check headers
        std::string content;
        EXPECT_TRUE(request->GetHeader("Content-Type", content));
        EXPECT_EQ(content, "text/plain");
        EXPECT_TRUE(request->GetHeader("User-Agent", content));
        EXPECT_EQ(content, "curl/7.64.1");
        EXPECT_TRUE(request->GetHeader("Accept", content));
        EXPECT_EQ(content, "*/*");

        // check pseudo headers
        EXPECT_EQ(request->GetMethod(), HttpMethod::HM_GET);
        EXPECT_EQ(request->GetPath(), "/");
        EXPECT_EQ(request->GetScheme(), "http");
        EXPECT_EQ(request->GetAuthority(), "localhost");

        // check body
        EXPECT_EQ(request->GetBody(), "Hello, Server!");

        // set response
        response->AddHeader("Content-Type", "text/plain");
        response->SetBody("Hello, Client!");
        response->SetStatusCode(400);
    };
    mock_server_->SetHttpHandler(http_handler);

    auto http_response_handler = [](std::shared_ptr<IResponse> response, uint32_t error) {
        // check headers
        std::string content_type;
        EXPECT_TRUE(response->GetHeader("Content-Type", content_type));
        EXPECT_EQ(content_type, "text/plain");

        // check body
        EXPECT_EQ(response->GetBody(), "Hello, Client!");
        // check status code
        EXPECT_EQ(response->GetStatusCode(), 400);
    };

    EXPECT_TRUE(mock_client_->DoRequest(request, http_response_handler));
}

}  // namespace
}  // namespace http3
}  // namespace quicx 