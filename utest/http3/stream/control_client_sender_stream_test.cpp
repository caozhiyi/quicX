#include <gtest/gtest.h>
#include "mock_quic_recv_stream.h"
#include "http3/stream/control_client_sender_stream.h"
#include "http3/stream/control_server_receiver_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockClientConnection {
public:
    MockClientConnection(std::shared_ptr<quic::IQuicSendStream> stream) {
        sender_stream_ = std::make_shared<ControlClientSenderStream>(stream,
            std::bind(&MockClientConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockClientConnection() {}

    bool SendMaxPushId(uint64_t push_id) {
        return sender_stream_->SendMaxPushId(push_id);
    }

    bool SendCancelPush(uint64_t push_id) {
        return sender_stream_->SendCancelPush(push_id);
    }

    bool SendGoAway(uint64_t stream_id) {
        return sender_stream_->SendGoaway(stream_id);
    }

    bool SendSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
        return sender_stream_->SendSettings(settings);
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    uint32_t GetErrorCode() { return error_code_; }
private:
    uint32_t error_code_;

    std::shared_ptr<ControlClientSenderStream> sender_stream_;
};

class MockServerConnection {
public:
    MockServerConnection(std::shared_ptr<quic::IQuicRecvStream> stream) {
        receiver_stream_ = std::make_shared<ControlServerReceiverStream>(stream,
            std::bind(&MockServerConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockServerConnection::GoAwayHandler, this, std::placeholders::_1),
            std::bind(&MockServerConnection::SettingHandler, this, std::placeholders::_1),
            std::bind(&MockServerConnection::MaxPushIdHandler, this, std::placeholders::_1),
            std::bind(&MockServerConnection::CancelHandler, this, std::placeholders::_1));
    }
    ~MockServerConnection() {}

    void GoAwayHandler(uint64_t id) {
        goaway_id_ = id;
    }

    void SettingHandler(const std::unordered_map<uint16_t, uint64_t>& settings) {
        settings_ = settings;
    }

    void MaxPushIdHandler(uint64_t push_id) {
        max_push_id_ = push_id;
    }
    
    void CancelHandler(uint64_t id) {
        cancel_id_ = id;
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    uint32_t GetErrorCode() { return error_code_; }
    uint64_t GetMaxPushId() { return max_push_id_; }
    uint64_t GetCancelId() { return cancel_id_; }
    uint64_t GetGoAwayId() { return goaway_id_; }
    const std::unordered_map<uint16_t, uint64_t>& GetSettings() { return settings_; }

private:
    uint32_t error_code_;
    uint64_t max_push_id_;
    uint64_t cancel_id_;
    uint64_t goaway_id_;
    std::unordered_map<uint16_t, uint64_t> settings_;

    std::shared_ptr<ControlServerReceiverStream> receiver_stream_;
};

class ControlClientSenderStreamTest
    : public testing::Test {
protected:
    void SetUp() override {
        mock_stream_1_ = std::make_shared<quic::MockQuicRecvStream>();
        mock_stream_2_ = std::make_shared<quic::MockQuicRecvStream>();

        mock_stream_1_->SetPeer(mock_stream_2_);
        mock_stream_2_->SetPeer(mock_stream_1_);
        
        client_connection_ = std::make_shared<MockClientConnection>(mock_stream_1_);
        server_connection_ = std::make_shared<MockServerConnection>(mock_stream_2_);
    }

    std::shared_ptr<quic::MockQuicRecvStream> mock_stream_1_;
    std::shared_ptr<quic::MockQuicRecvStream> mock_stream_2_;
    std::shared_ptr<MockClientConnection> client_connection_;
    std::shared_ptr<MockServerConnection> server_connection_;
};

TEST_F(ControlClientSenderStreamTest, SendMaxPushId) {
    client_connection_->SendMaxPushId(100);
    EXPECT_EQ(server_connection_->GetMaxPushId(), 100);
}

TEST_F(ControlClientSenderStreamTest, SendCancelPush) {
    client_connection_->SendCancelPush(200);
    EXPECT_EQ(server_connection_->GetCancelId(), 200);
}

TEST_F(ControlClientSenderStreamTest, SendGoAway) {
    client_connection_->SendGoAway(300);
    EXPECT_EQ(server_connection_->GetGoAwayId(), 300);
}   

TEST_F(ControlClientSenderStreamTest, SendSettings) {
    std::unordered_map<uint16_t, uint64_t> settings = {{1, 100}, {2, 200}};
    client_connection_->SendSettings(settings);
    EXPECT_EQ(server_connection_->GetSettings(), settings);
}

}  // namespace
}  // namespace http3
}  // namespace quicx 