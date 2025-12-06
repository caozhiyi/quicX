#include <limits>
#include <unordered_map>
#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/http/error.h"
#include "http3/frame/goaway_frame.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/stream/control_server_receiver_stream.h"
#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

class ControlStreamRfcTest:
    public ::testing::Test {
protected:
    void SetUp() override {
        client_stream_ = std::make_shared<quic::MockQuicStream>();
        server_stream_ = std::make_shared<quic::MockQuicStream>();

        client_stream_->SetPeer(server_stream_);
        server_stream_->SetPeer(client_stream_);

        qpack_encoder_ = std::make_shared<QpackEncoder>();

        auto error_cb = [this](uint64_t /*stream_id*/, uint32_t error_code) {
            last_error_ = error_code;
        };
        auto goaway_cb = [this](uint64_t id) {
            last_goaway_id_ = id;
        };
        auto settings_cb = [this](const std::unordered_map<uint16_t, uint64_t>& settings) {
            last_settings_ = settings;
        };
        auto max_push_cb = [this](uint64_t id) {
            last_max_push_id_ = id;
        };
        auto cancel_cb = [this](uint64_t id) {
            last_cancel_id_ = id;
        };

        receiver_stream_ = std::make_shared<ControlServerReceiverStream>(
            server_stream_, qpack_encoder_, error_cb, goaway_cb, settings_cb, max_push_cb, cancel_cb);
    }

    void TearDown() override {
        receiver_stream_.reset();
        client_stream_.reset();
        server_stream_.reset();
    }

    void SendFrame(IFrame& frame) {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(256);
        auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
        ASSERT_TRUE(frame.Encode(buffer));
        client_stream_->Send(buffer);
    }

    void SendSettings(const std::unordered_map<uint16_t, uint64_t>& settings = {}) {
        SettingsFrame frame;
        for (const auto& kv : settings) {
            frame.SetSetting(kv.first, kv.second);
        }
        SendFrame(frame);
    }

    void SendGoAway(uint64_t id) {
        GoAwayFrame frame;
        frame.SetStreamId(id);
        SendFrame(frame);
    }

    void SendMaxPushId(uint64_t id) {
        MaxPushIdFrame frame;
        frame.SetPushId(id);
        SendFrame(frame);
    }

protected:
    std::shared_ptr<quic::MockQuicStream> client_stream_;
    std::shared_ptr<quic::MockQuicStream> server_stream_;
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<ControlServerReceiverStream> receiver_stream_;

    uint32_t last_error_{0};
    uint64_t last_goaway_id_{std::numeric_limits<uint64_t>::max()};
    std::unordered_map<uint16_t, uint64_t> last_settings_;
    uint64_t last_max_push_id_{0};
    uint64_t last_cancel_id_{0};
};

TEST_F(ControlStreamRfcTest, FirstFrameMustBeSettings) {
    SendGoAway(0);
    EXPECT_EQ(last_error_, Http3ErrorCode::kFrameUnexpected);
}

TEST_F(ControlStreamRfcTest, DuplicateSettingsTriggersSettingsError) {
    SendSettings();
    EXPECT_EQ(last_error_, 0u);
    SendSettings();
    EXPECT_EQ(last_error_, Http3ErrorCode::kSettingsError);
}

TEST_F(ControlStreamRfcTest, MaxPushIdMustBeMonotonic) {
    SendSettings();
    EXPECT_EQ(last_error_, 0u);

    SendMaxPushId(5);
    EXPECT_EQ(last_error_, 0u);
    EXPECT_EQ(last_max_push_id_, 5u);

    SendMaxPushId(4);
    EXPECT_EQ(last_error_, Http3ErrorCode::kIdError);
}

TEST_F(ControlStreamRfcTest, GoAwayMustNotIncrease) {
    SendSettings();
    EXPECT_EQ(last_error_, 0u);

    SendGoAway(100);
    EXPECT_EQ(last_error_, 0u);
    EXPECT_EQ(last_goaway_id_, 100u);

    SendGoAway(200);
    EXPECT_EQ(last_error_, Http3ErrorCode::kIdError);
}

}  // namespace
}  // namespace http3
}  // namespace quicx


