#include <gtest/gtest.h>

#include "quic/packet/header/long_header.h"
#include "quic/common/constants.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
namespace {

// Test that decoding a Long Header with CID length > kMaxConnectionLength fails
TEST(long_header_boundary_utest, oversized_dcid_length) {
    static const uint8_t kBufLen = 128;
    auto buffer = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(kBufLen));

    // Manually construct a malicious long header packet:
    // Flag(1) + Version(4) + DCID_Len(1) + DCID(N) + SCID_Len(1) + SCID(N)
    uint8_t malicious_packet[64] = {0};
    uint8_t pos = 0;

    // Header flag: long header form bit set + fixed bit set
    malicious_packet[pos++] = 0xC0; // 1100_0000

    // Version: 1
    malicious_packet[pos++] = 0x00;
    malicious_packet[pos++] = 0x00;
    malicious_packet[pos++] = 0x00;
    malicious_packet[pos++] = 0x01;

    // DCID length: 255 (exceeds kMaxConnectionLength=20)
    malicious_packet[pos++] = 0xFF;

    // Write just enough bytes for the buffer
    buffer->Write(malicious_packet, pos + 30);

    LongHeader header;
    EXPECT_FALSE(header.DecodeHeader(buffer, true));
}

TEST(long_header_boundary_utest, oversized_scid_length) {
    static const uint8_t kBufLen = 128;
    auto buffer = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(kBufLen));

    uint8_t malicious_packet[64] = {0};
    uint8_t pos = 0;

    // Header flag
    malicious_packet[pos++] = 0xC0;

    // Version: 1
    malicious_packet[pos++] = 0x00;
    malicious_packet[pos++] = 0x00;
    malicious_packet[pos++] = 0x00;
    malicious_packet[pos++] = 0x01;

    // DCID length: 4 (valid)
    malicious_packet[pos++] = 0x04;
    malicious_packet[pos++] = 0x01;
    malicious_packet[pos++] = 0x02;
    malicious_packet[pos++] = 0x03;
    malicious_packet[pos++] = 0x04;

    // SCID length: 255 (exceeds kMaxConnectionLength=20)
    malicious_packet[pos++] = 0xFF;

    buffer->Write(malicious_packet, pos + 30);

    LongHeader header;
    EXPECT_FALSE(header.DecodeHeader(buffer, true));
}

TEST(long_header_boundary_utest, max_valid_cid_length) {
    static const uint8_t kBufLen = 128;
    auto buffer = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(kBufLen));

    uint8_t packet[128] = {0};
    uint8_t pos = 0;

    // Header flag
    packet[pos++] = 0xC0;

    // Version: 1
    packet[pos++] = 0x00;
    packet[pos++] = 0x00;
    packet[pos++] = 0x00;
    packet[pos++] = 0x01;

    // DCID length: kMaxConnectionLength (20) - should succeed
    packet[pos++] = kMaxConnectionLength;
    for (uint8_t i = 0; i < kMaxConnectionLength; i++) {
        packet[pos++] = i + 1;
    }

    // SCID length: kMaxConnectionLength (20) - should succeed
    packet[pos++] = kMaxConnectionLength;
    for (uint8_t i = 0; i < kMaxConnectionLength; i++) {
        packet[pos++] = i + 0x10;
    }

    buffer->Write(packet, pos);

    LongHeader header;
    EXPECT_TRUE(header.DecodeHeader(buffer, true));
    EXPECT_EQ(header.GetDestinationConnectionIdLength(), kMaxConnectionLength);
    EXPECT_EQ(header.GetSourceConnectionIdLength(), kMaxConnectionLength);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
