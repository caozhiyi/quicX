#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/multi_block_buffer_decode_wrapper.h"
#include "common/buffer/single_block_buffer.h"
#include "common/decode/decode.h"
#include "common/log/log.h"
#include "common/log/stdout_logger.h"

namespace quicx {
namespace common {
namespace {

class MultiBlockBufferDecodeWrapperTest: public testing::Test {
protected:
    void SetUp() override {
        std::shared_ptr<Logger> std_log = std::make_shared<StdoutLogger>();
        LOG_SET(std_log);
        LOG_SET_LEVEL(LogLevel::kDebug);
    }

    void TearDown() override { LOG_SET_LEVEL(LogLevel::kNull); }
};

// Helper function to create a pool
std::shared_ptr<BlockMemoryPool> MakePool(uint32_t size = 64, uint32_t count = 4) {
    return MakeBlockMemoryPoolPtr(size, count);
}

// ============================================================================
// Basic Construction and Lifecycle Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, Construction) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    EXPECT_EQ(0u, wrapper.GetReadLength());
    EXPECT_EQ(0u, wrapper.GetDataLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, DestructorFlushes) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    // Write some data
    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x42);
    encoder.Flush();

    EXPECT_EQ(1u, buffer->GetDataLength());

    {
        MultiBlockBufferDecodeWrapper wrapper(buffer);
        uint8_t value = 0;
        EXPECT_TRUE(wrapper.DecodeFixedUint8(value));
        EXPECT_EQ(0x42, value);
        // Destructor should flush
    }

    EXPECT_EQ(0u, buffer->GetDataLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, CancelDecode) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x42);
    encoder.Flush();

    EXPECT_EQ(1u, buffer->GetDataLength());

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint8_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(value));
    EXPECT_EQ(0x42, value);

    wrapper.CancelDecode();
    wrapper.Flush();  // Should not move buffer pointer

    // Buffer should still have data since we cancelled
    EXPECT_EQ(1u, buffer->GetDataLength());
}

// ============================================================================
// DecodeFixedUint8 Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint8Basic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0xAB);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint8_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(value));
    EXPECT_EQ(0xAB, value);
    EXPECT_EQ(1u, wrapper.GetReadLength());

    wrapper.Flush();
    EXPECT_EQ(0u, buffer->GetDataLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint8Multiple) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x01);
    encoder.EncodeFixedUint8(0x02);
    encoder.EncodeFixedUint8(0x03);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);

    uint8_t v1 = 0, v2 = 0, v3 = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v1));
    EXPECT_EQ(0x01, v1);
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v2));
    EXPECT_EQ(0x02, v2);
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v3));
    EXPECT_EQ(0x03, v3);

    EXPECT_EQ(3u, wrapper.GetReadLength());
    wrapper.Flush();
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint8InsufficientData) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint8_t value = 0;
    EXPECT_FALSE(wrapper.DecodeFixedUint8(value));
}

// ============================================================================
// DecodeFixedUint16 Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint16Basic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint16(0x1234);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint16_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint16(value));
    EXPECT_EQ(0x1234, value);
    EXPECT_EQ(2u, wrapper.GetReadLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint16InsufficientData) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x12);  // Only 1 byte
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint16_t value = 0;
    EXPECT_FALSE(wrapper.DecodeFixedUint16(value));
}

// ============================================================================
// DecodeFixedUint32 Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint32Basic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint32(0xABCDEF01);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint32_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint32(value));
    EXPECT_EQ(0xABCDEF01, value);
    EXPECT_EQ(4u, wrapper.GetReadLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint32Multiple) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint32(0x11111111);
    encoder.EncodeFixedUint32(0x22222222);
    encoder.EncodeFixedUint32(0x33333333);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);

    uint32_t v1 = 0, v2 = 0, v3 = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint32(v1));
    EXPECT_EQ(0x11111111, v1);
    EXPECT_TRUE(wrapper.DecodeFixedUint32(v2));
    EXPECT_EQ(0x22222222, v2);
    EXPECT_TRUE(wrapper.DecodeFixedUint32(v3));
    EXPECT_EQ(0x33333333, v3);

    EXPECT_EQ(12u, wrapper.GetReadLength());
}

// ============================================================================
// DecodeFixedUint64 Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, DecodeFixedUint64Basic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint64(0x0123456789ABCDEFULL);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint64_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint64(value));
    EXPECT_EQ(0x0123456789ABCDEFULL, value);
    EXPECT_EQ(8u, wrapper.GetReadLength());
}

// ============================================================================
// DecodeVarint Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintUint64_1Byte) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeVarint<uint64_t>(42);  // 1-byte varint
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint64_t value = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(value));
    EXPECT_EQ(42u, value);
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintUint64_2Byte) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeVarint<uint64_t>(300);  // 2-byte varint
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint64_t value = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(value));
    EXPECT_EQ(300u, value);
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintUint64_4Byte) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeVarint<uint64_t>(70000);  // 4-byte varint
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint64_t value = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(value));
    EXPECT_EQ(70000u, value);
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintUint64_8Byte) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeVarint<uint64_t>(0x3FFFFFFFFFFFFFFFULL);  // 8-byte varint
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint64_t value = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(value));
    EXPECT_EQ(0x3FFFFFFFFFFFFFFFULL, value);
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintUint32) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeVarint<uint32_t>(12345);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint32_t value = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(value));
    EXPECT_EQ(12345u, value);
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintInsufficientData) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    // Write incomplete varint (only 1 byte, but varint needs more)
    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0xC0);  // First byte of a 2-byte varint
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint64_t value = 0;
    EXPECT_FALSE(wrapper.DecodeVarint(value));
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintMultiple) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeVarint<uint64_t>(10);
    encoder.EncodeVarint<uint64_t>(20);
    encoder.EncodeVarint<uint64_t>(30);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);

    uint64_t v1 = 0, v2 = 0, v3 = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(v1));
    EXPECT_EQ(10u, v1);
    EXPECT_TRUE(wrapper.DecodeVarint(v2));
    EXPECT_EQ(20u, v2);
    EXPECT_TRUE(wrapper.DecodeVarint(v3));
    EXPECT_EQ(30u, v3);
}

// ============================================================================
// Round-Trip Tests (Encode then Decode)
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, RoundTripMixedTypes) {
    auto pool = MakePool(128, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    // Encode
    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x11);
    encoder.EncodeFixedUint16(0x2233);
    encoder.EncodeFixedUint32(0x44556677);
    encoder.EncodeFixedUint64(0x8899AABBCCDDEEFFULL);
    encoder.EncodeVarint<uint64_t>(123456789);
    encoder.Flush();

    // Decode
    MultiBlockBufferDecodeWrapper wrapper(buffer);

    uint8_t v8 = 0;
    uint16_t v16 = 0;
    uint32_t v32 = 0;
    uint64_t v64 = 0;
    uint64_t varint = 0;

    EXPECT_TRUE(wrapper.DecodeFixedUint8(v8));
    EXPECT_EQ(0x11, v8);

    EXPECT_TRUE(wrapper.DecodeFixedUint16(v16));
    EXPECT_EQ(0x2233, v16);

    EXPECT_TRUE(wrapper.DecodeFixedUint32(v32));
    EXPECT_EQ(0x44556677, v32);

    EXPECT_TRUE(wrapper.DecodeFixedUint64(v64));
    EXPECT_EQ(0x8899AABBCCDDEEFFULL, v64);

    EXPECT_TRUE(wrapper.DecodeVarint(varint));
    EXPECT_EQ(123456789u, varint);

    wrapper.Flush();
    EXPECT_EQ(0u, buffer->GetDataLength());
}

// ============================================================================
// Flush and Cancel Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, FlushAdvancesBuffer) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0xAA);
    encoder.EncodeFixedUint8(0xBB);
    encoder.Flush();

    EXPECT_EQ(2u, buffer->GetDataLength());

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint8_t v1 = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v1));
    EXPECT_EQ(0xAA, v1);

    // Buffer not moved yet
    EXPECT_EQ(2u, buffer->GetDataLength());

    wrapper.Flush();
    // Buffer moved forward by 1 byte
    EXPECT_EQ(1u, buffer->GetDataLength());

    // Can decode the second byte
    uint8_t v2 = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v2));
    EXPECT_EQ(0xBB, v2);
}

TEST(MultiBlockBufferDecodeWrapperTest, CancelDecodeDoesNotAdvance) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0xAA);
    encoder.Flush();

    EXPECT_EQ(1u, buffer->GetDataLength());

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint8_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(value));
    EXPECT_EQ(0xAA, value);

    wrapper.CancelDecode();
    wrapper.Flush();

    // Buffer should not be advanced
    EXPECT_EQ(1u, buffer->GetDataLength());

    // Can decode again
    MultiBlockBufferDecodeWrapper wrapper2(buffer);
    uint8_t value2 = 0;
    EXPECT_TRUE(wrapper2.DecodeFixedUint8(value2));
    EXPECT_EQ(0xAA, value2);
}

// ============================================================================
// Multi-Block Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, DecodeAcrossMultipleBlocks) {
    auto pool = MakePool(32, 4);  // Small chunk size
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    // Write data that spans multiple chunks
    // Manually encode each uint32_t and write to buffer using Write() method
    // This ensures data spans across multiple blocks (10 * 4 = 40 bytes > 32 bytes chunk size)
    uint8_t encode_buf[4];
    for (uint32_t i = 0; i < 10; ++i) {
        uint8_t* encoded_end = common::FixedEncodeUint32(encode_buf, encode_buf + 4, i);
        EXPECT_NE(nullptr, encoded_end);
        uint32_t encoded_len = static_cast<uint32_t>(encoded_end - encode_buf);
        buffer->Write(encode_buf, encoded_len);
    }

    MultiBlockBufferDecodeWrapper wrapper(buffer);

    // Decode all values
    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t value = 0;
        EXPECT_TRUE(wrapper.DecodeFixedUint32(value));
        EXPECT_EQ(i, value);
    }

    wrapper.Flush();
    EXPECT_EQ(0u, buffer->GetDataLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeVarintAcrossBlocks) {
    auto pool = MakePool(32, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    // Write varints that might span blocks
    // Manually encode each varint and write to buffer using Write() method
    // This ensures varints can span across multiple blocks
    uint8_t encode_buf[8];  // Max varint size is 8 bytes
    uint64_t values[] = {100, 200, 300};
    for (uint64_t val : values) {
        uint8_t* encoded_end = common::EncodeVarint(encode_buf, encode_buf + 8, val);
        EXPECT_NE(nullptr, encoded_end);
        uint32_t encoded_len = static_cast<uint32_t>(encoded_end - encode_buf);
        buffer->Write(encode_buf, encoded_len);
    }

    MultiBlockBufferDecodeWrapper wrapper(buffer);

    uint64_t v1 = 0, v2 = 0, v3 = 0;
    EXPECT_TRUE(wrapper.DecodeVarint(v1));
    EXPECT_EQ(100u, v1);
    EXPECT_TRUE(wrapper.DecodeVarint(v2));
    EXPECT_EQ(200u, v2);
    EXPECT_TRUE(wrapper.DecodeVarint(v3));
    EXPECT_EQ(300u, v3);
}

// ============================================================================
// GetDataLength and GetReadLength Tests
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, GetDataLength) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x11);
    encoder.EncodeFixedUint8(0x22);
    encoder.EncodeFixedUint8(0x33);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    EXPECT_EQ(3u, wrapper.GetDataLength());

    uint8_t v = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v));
    EXPECT_EQ(2u, wrapper.GetDataLength());

    EXPECT_TRUE(wrapper.DecodeFixedUint8(v));
    EXPECT_EQ(1u, wrapper.GetDataLength());

    EXPECT_TRUE(wrapper.DecodeFixedUint8(v));
    EXPECT_EQ(0u, wrapper.GetDataLength());
}

TEST(MultiBlockBufferDecodeWrapperTest, GetReadLength) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0x11);
    encoder.EncodeFixedUint8(0x22);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    EXPECT_EQ(0u, wrapper.GetReadLength());

    uint8_t v = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v));
    EXPECT_EQ(1u, wrapper.GetReadLength());

    EXPECT_TRUE(wrapper.DecodeFixedUint8(v));
    EXPECT_EQ(2u, wrapper.GetReadLength());
}

// ============================================================================
// Integration with SingleBlockBuffer
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, WithSingleBlockBuffer) {
    auto pool = MakePool(64, 4);
    auto chunk = std::make_shared<BufferChunk>(pool);
    auto buffer = std::make_shared<SingleBlockBuffer>(chunk);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint32(0x12345678);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint32_t value = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint32(value));
    EXPECT_EQ(0x12345678, value);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(MultiBlockBufferDecodeWrapperTest, EmptyBuffer) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    EXPECT_EQ(0u, wrapper.GetDataLength());
    EXPECT_EQ(0u, wrapper.GetReadLength());

    uint8_t value = 0;
    EXPECT_FALSE(wrapper.DecodeFixedUint8(value));
}

TEST(MultiBlockBufferDecodeWrapperTest, DecodeAfterFlush) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferEncodeWrapper encoder(buffer);
    encoder.EncodeFixedUint8(0xAA);
    encoder.EncodeFixedUint8(0xBB);
    encoder.Flush();

    MultiBlockBufferDecodeWrapper wrapper(buffer);
    uint8_t v1 = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v1));
    wrapper.Flush();

    // Can still decode more
    uint8_t v2 = 0;
    EXPECT_TRUE(wrapper.DecodeFixedUint8(v2));
    EXPECT_EQ(0xBB, v2);
}

}  // namespace
}  // namespace common
}  // namespace quicx
