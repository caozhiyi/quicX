#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "http3/stream/unidentified_stream.h"

#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

// Regression tests for the QX-01 fix: UnidentifiedStream wrappers must be
// released (error handler invoked, read callback unregistered) whenever the
// transport stream terminates before a complete stream-type varint arrives —
// FIN before the type byte, FIN inside a partial varint, or RESET with any
// application error code including 0. Previously these wrappers (and the
// underlying QUIC streams) were retained forever, allowing unauthenticated
// memory exhaustion.
class UnidentifiedStreamLeakTest: public testing::Test {
protected:
    void SetUp() override {
        mock_stream_ = std::make_shared<quic::MockQuicStream>(9, StreamDirection::kRecv);

        error_handler_called_ = false;
        error_stream_id_ = 0;
        error_code_ = 0xFFFFFFFF;
        identified_type_ = 0;
        identified_called_ = false;

        unidentified_ = std::make_shared<UnidentifiedStream>(mock_stream_,
            [this](uint64_t stream_id, uint32_t code) {
                error_handler_called_ = true;
                error_stream_id_ = stream_id;
                error_code_ = code;
            },
            [this](uint64_t stream_type, std::shared_ptr<IQuicRecvStream>, std::shared_ptr<IBufferRead>) {
                identified_called_ = true;
                identified_type_ = stream_type;
            });
    }

    // Build a readable buffer holding the given bytes (the attack payloads
    // from the report: single varint prefix bytes).
    static std::shared_ptr<common::SingleBlockBuffer> MakeData(const std::vector<uint8_t>& bytes) {
        auto buffer = std::make_shared<common::SingleBlockBuffer>(
            std::make_shared<common::StandaloneBufferChunk>(bytes.size() > 0 ? bytes.size() : 1));
        if (!bytes.empty()) {
            buffer->Write(const_cast<uint8_t*>(bytes.data()), bytes.size());
        }
        return buffer;
    }

    std::shared_ptr<quic::MockQuicStream> mock_stream_;
    std::shared_ptr<UnidentifiedStream> unidentified_;

    bool error_handler_called_;
    uint64_t error_stream_id_;
    uint32_t error_code_;
    bool identified_called_;
    uint64_t identified_type_;
};

// PoC variant A: exactly one byte 0x40 (the 2-byte varint prefix) and then
// FIN — the stream type can never be completed. The wrapper must be released
// instead of retained forever.
TEST_F(UnidentifiedStreamLeakTest, FinInsidePartialVarintReleasesWrapper) {
    mock_stream_->SimulateRead(MakeData({0x40}), true, 0);

    EXPECT_TRUE(error_handler_called_);
    EXPECT_EQ(error_stream_id_, 9u);
    EXPECT_EQ(error_code_, 0u);
    EXPECT_FALSE(identified_called_);
    // The read callback must be unregistered so no further data path keeps
    // the wrapper alive.
    EXPECT_TRUE(mock_stream_->ReadCb() == nullptr);
}

// FIN delivered before any type byte at all.
TEST_F(UnidentifiedStreamLeakTest, FinBeforeTypeByteReleasesWrapper) {
    mock_stream_->SimulateRead(MakeData({}), true, 0);

    EXPECT_TRUE(error_handler_called_);
    EXPECT_EQ(error_stream_id_, 9u);
    EXPECT_FALSE(identified_called_);
    EXPECT_TRUE(mock_stream_->ReadCb() == nullptr);
}

// PoC variant B: RESET_STREAM with application error code 0. The transport
// surfaces this as (is_last=true, error=0); the wrapper must still release.
TEST_F(UnidentifiedStreamLeakTest, ResetWithErrorCodeZeroReleasesWrapper) {
    mock_stream_->SimulateRead(MakeData({0x40}), true, 0);

    EXPECT_TRUE(error_handler_called_);
    EXPECT_EQ(error_code_, 0u);
    EXPECT_TRUE(mock_stream_->ReadCb() == nullptr);
}

// RESET with a non-zero application error code: released and the error code
// is propagated to the error handler.
TEST_F(UnidentifiedStreamLeakTest, ResetWithErrorCodeReleasesAndPropagates) {
    mock_stream_->SimulateRead(MakeData({0x40}), false, 0x100);

    EXPECT_TRUE(error_handler_called_);
    EXPECT_EQ(error_stream_id_, 9u);
    EXPECT_EQ(error_code_, 0x100u);
    EXPECT_FALSE(identified_called_);
    EXPECT_TRUE(mock_stream_->ReadCb() == nullptr);
}

// Control: a complete varint type followed by FIN must still dispatch to the
// owner (no false release), even when FIN arrives in the same delivery.
TEST_F(UnidentifiedStreamLeakTest, CompleteVarintWithFinStillDispatches) {
    mock_stream_->SimulateRead(MakeData({0x00}), true, 0);

    EXPECT_TRUE(identified_called_);
    EXPECT_EQ(identified_type_, 0u);
    EXPECT_FALSE(error_handler_called_);
}

// Control: a partial varint without FIN keeps waiting (legitimate slow
// sender must not be released early).
TEST_F(UnidentifiedStreamLeakTest, PartialVarintWithoutFinKeepsWaiting) {
    mock_stream_->SimulateRead(MakeData({0x40}), false, 0);

    EXPECT_FALSE(error_handler_called_);
    EXPECT_FALSE(identified_called_);
    EXPECT_TRUE(mock_stream_->ReadCb() != nullptr);
}

}  // namespace
}  // namespace http3
}  // namespace quicx
