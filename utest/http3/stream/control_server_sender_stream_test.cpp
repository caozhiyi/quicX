#include <gtest/gtest.h>
#include "mock_quic_recv_stream.h"
#include "http3/stream/control_sender_stream.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockClientConnection {
public:
    MockClientConnection(std::shared_ptr<quic::IQuicRecvStream> stream) {
        receiver_stream_ = std::make_shared<ControlReceiverStream>(stream,
            std::bind(&MockClientConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockClientConnection::GoAwayHandler, this, std::placeholders::_1),
            std::bind(&MockClientConnection::SettingHandler, this, std::placeholders::_1));
    }
    ~MockClientConnection() {}

    void GoAwayHandler(uint64_t id) {
        goaway_id_ = id;
    }

    void SettingHandler(const std::unordered_map<uint16_t, uint64_t>& settings) {
        settings_ = settings;
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    uint32_t GetErrorCode() { return error_code_; }
    uint64_t GetGoAwayId() { return goaway_id_; }
    const std::unordered_map<uint16_t, uint64_t>& GetSettings() { return settings_; }
private:
    uint32_t error_code_;
    uint64_t goaway_id_;
    std::unordered_map<uint16_t, uint64_t> settings_;

    std::shared_ptr<ControlReceiverStream> receiver_stream_;
};

class MockServerConnection {
public:
    MockServerConnection(std::shared_ptr<quic::IQuicSendStream> stream) {
        sender_stream_ = std::make_shared<ControlSenderStream>(stream,
            std::bind(&MockServerConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockServerConnection() {}

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
    std::shared_ptr<ControlSenderStream> sender_stream_;
};

class ControlServerSenderStreamTest
    : public testing::Test {
protected:
    void SetUp() override {
        mock_stream_ = std::make_shared<quic::MockQuicRecvStream>();
        client_connection_ = std::make_shared<MockClientConnection>(mock_stream_);
        server_connection_ = std::make_shared<MockServerConnection>(mock_stream_);
    }

    std::shared_ptr<quic::MockQuicRecvStream> mock_stream_;
    std::shared_ptr<MockClientConnection> client_connection_;
    std::shared_ptr<MockServerConnection> server_connection_;
};

TEST_F(ControlServerSenderStreamTest, SendGoAway) {
    server_connection_->SendGoAway(300);
    EXPECT_EQ(client_connection_->GetGoAwayId(), 300);
}   

TEST_F(ControlServerSenderStreamTest, SendSettings) {
    std::unordered_map<uint16_t, uint64_t> settings = {{1, 100}, {2, 200}};
    server_connection_->SendSettings(settings);
    EXPECT_EQ(client_connection_->GetSettings(), settings);
}

}  // namespace
}  // namespace http3
}  // namespace quicx 