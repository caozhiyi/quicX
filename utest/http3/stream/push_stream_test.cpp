#include <gtest/gtest.h>
#include "http3/http/response.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/push_sender_stream.h"
#include "http3/stream/push_receiver_stream.h"
#include "utest/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockClientConnection {
public:
    MockClientConnection(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        std::shared_ptr<quic::IQuicRecvStream> stream) {
        receiver_stream_ = std::make_shared<PushReceiverStream>(qpack_encoder, stream,
            std::bind(&MockClientConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockClientConnection::ResponseHandler, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockClientConnection() {}

    void ResponseHandler(std::shared_ptr<IResponse> response, uint32_t error) {
        response_ = response;
        error_code_ = error;
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    const std::shared_ptr<IResponse>& GetResponse() { return response_; }
    uint32_t GetErrorCode() { return error_code_; }
private:
    uint32_t error_code_;
    std::shared_ptr<IResponse> response_;

    std::shared_ptr<PushReceiverStream> receiver_stream_;
};

class MockServerConnection {
public:
    MockServerConnection(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        std::shared_ptr<quic::IQuicSendStream> stream) {
        sender_stream_ = std::make_shared<PushSenderStream>(qpack_encoder, stream,
            std::bind(&MockServerConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            0);
    }
    ~MockServerConnection() {}

    bool SendPushResponse(std::shared_ptr<IResponse> response) {
        return sender_stream_->SendPushResponse(response);
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    uint32_t GetErrorCode() { return error_code_; }
private:
    uint32_t error_code_;

    std::shared_ptr<PushSenderStream> sender_stream_;
};

class PushStreamTest
    : public testing::Test {
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

TEST_F(PushStreamTest, SendHeaders) {
    std::shared_ptr<IResponse> response = std::make_shared<Response>();
    response->AddHeader("Content-Type", "text/plain");

    EXPECT_TRUE(server_connection_->SendPushResponse(response));

    std::string content_type;
    EXPECT_TRUE(client_connection_->GetResponse()->GetHeader("Content-Type", content_type));
    EXPECT_EQ(content_type, "text/plain");

    EXPECT_EQ(client_connection_->GetResponse()->GetStatusCode(), 200);
}

TEST_F(PushStreamTest, SendHeadersAndBody) {
    std::shared_ptr<IResponse> response = std::make_shared<Response>();
    response->AddHeader("Content-Type", "text/plain");
    response->SetBody("Hello, World!");
    EXPECT_TRUE(server_connection_->SendPushResponse(response));
    EXPECT_EQ(client_connection_->GetResponse()->GetBody(), "Hello, World!");
}

TEST_F(PushStreamTest, SendBody) {
    std::shared_ptr<IResponse> response = std::make_shared<Response>();
    response->SetBody("Hello, World!");
    EXPECT_TRUE(server_connection_->SendPushResponse(response));
    EXPECT_EQ(client_connection_->GetResponse()->GetBody(), "Hello, World!");
}

}  // namespace
}  // namespace http3
}  // namespace quicx