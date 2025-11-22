#include <vector>

#include <gtest/gtest.h>

#include "http3/http/error.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/frame/qpack_encoder_frames.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "unit_test/http3/stream/mock_quic_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"

namespace quicx {
namespace http3 {
namespace {

class QpackStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        encoder_send_stream_ = std::make_shared<quic::MockQuicStream>();
        encoder_recv_stream_ = std::make_shared<quic::MockQuicStream>();
        encoder_send_stream_->SetPeer(encoder_recv_stream_);
        encoder_recv_stream_->SetPeer(encoder_send_stream_);

        decoder_send_stream_ = std::make_shared<quic::MockQuicStream>();
        decoder_recv_stream_ = std::make_shared<quic::MockQuicStream>();
        decoder_send_stream_->SetPeer(decoder_recv_stream_);
        decoder_recv_stream_->SetPeer(decoder_send_stream_);

        encoder_registry_ = std::make_shared<QpackBlockedRegistry>();
        decoder_registry_ = std::make_shared<QpackBlockedRegistry>();

        auto encoder_error_cb = [this](uint64_t /*stream_id*/, uint32_t error_code) {
            encoder_error_code_ = error_code;
        };
        auto decoder_error_cb = [this](uint64_t /*stream_id*/, uint32_t error_code) {
            decoder_error_code_ = error_code;
        };

        encoder_sender_ = std::make_shared<QpackEncoderSenderStream>(encoder_send_stream_, encoder_error_cb);
        encoder_receiver_ = std::make_shared<QpackEncoderReceiverStream>(encoder_recv_stream_, encoder_registry_, encoder_error_cb);

        decoder_sender_ = std::make_shared<QpackDecoderSenderStream>(decoder_send_stream_, decoder_error_cb);
        decoder_receiver_ = std::make_shared<QpackDecoderReceiverStream>(decoder_recv_stream_, decoder_registry_, decoder_error_cb);
    }

    void TearDown() override {
        encoder_sender_.reset();
        encoder_receiver_.reset();
        decoder_sender_.reset();
        decoder_receiver_.reset();
        encoder_send_stream_.reset();
        encoder_recv_stream_.reset();
        decoder_send_stream_.reset();
        decoder_recv_stream_.reset();
        encoder_registry_.reset();
        decoder_registry_.reset();
    }

protected:
    std::shared_ptr<quic::MockQuicStream> encoder_send_stream_;
    std::shared_ptr<quic::MockQuicStream> encoder_recv_stream_;
    std::shared_ptr<quic::MockQuicStream> decoder_send_stream_;
    std::shared_ptr<quic::MockQuicStream> decoder_recv_stream_;

    std::shared_ptr<QpackBlockedRegistry> encoder_registry_;
    std::shared_ptr<QpackBlockedRegistry> decoder_registry_;

    std::shared_ptr<QpackEncoderSenderStream> encoder_sender_;
    std::shared_ptr<QpackEncoderReceiverStream> encoder_receiver_;
    std::shared_ptr<QpackDecoderSenderStream> decoder_sender_;
    std::shared_ptr<QpackDecoderReceiverStream> decoder_receiver_;

    uint32_t encoder_error_code_{0};
    uint32_t decoder_error_code_{0};
    uint32_t encoder_notify_count_{0};
    uint32_t decoder_ack_count_{0};
    uint32_t decoder_notify_count_{0};
};

TEST_F(QpackStreamTest, EncoderSenderBasicInstructionsSucceed) {
    // Send a simple name/value insert then a duplicate; just ensure no errors are reported.
    EXPECT_TRUE(encoder_sender_->SendInsertWithoutNameRef("x-name", "x-value"));
    EXPECT_EQ(encoder_error_code_, 0u);
    EXPECT_TRUE(encoder_sender_->SendDuplicate(7));
    EXPECT_EQ(encoder_error_code_, 0u);
}

TEST_F(QpackStreamTest, EncoderReceiverNotifiesBlockedStreams) {
    uint64_t key = (1ULL << 32) | 1ULL;
    encoder_registry_->Add(key, [this]() { encoder_notify_count_++; });

    QpackSetCapacityFrame frame;
    frame.SetCapacity(128);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(64);
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(frame.Encode(buffer));
    encoder_send_stream_->Send(buffer);

    EXPECT_EQ(encoder_error_code_, 0u);
    EXPECT_EQ(encoder_notify_count_, 1u);
    EXPECT_EQ(encoder_registry_->GetBlockedCount(), 0u);
}

TEST_F(QpackStreamTest, DecoderSenderReceiverRoundTrip) {
    uint64_t header_block_id = (5ULL << 32) | 3ULL;
    decoder_registry_->Add(header_block_id, [this]() { decoder_ack_count_++; });

    EXPECT_TRUE(decoder_sender_->SendSectionAck(header_block_id));

    EXPECT_EQ(decoder_error_code_, 0u);
    EXPECT_EQ(decoder_ack_count_, 1u);
    EXPECT_EQ(decoder_registry_->GetBlockedCount(), 0u);

    decoder_registry_->Add(0xABCDULL, [this]() { decoder_notify_count_++; });
    EXPECT_TRUE(decoder_sender_->SendInsertCountIncrement(4));
    EXPECT_EQ(decoder_error_code_, 0u);
    EXPECT_EQ(decoder_notify_count_, 1u);
}

}  // namespace
}  // namespace http3
}  // namespace quicx


