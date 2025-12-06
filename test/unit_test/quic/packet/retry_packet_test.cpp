#include <gtest/gtest.h>

#include "quic/packet/retry_packet.h"
#include "quic/packet/retry_packet.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
namespace {

TEST(retry_packet_utest, codec) {
    uint8_t tag[kRetryIntegrityTagLength];
    for (size_t i = 0; i < kRetryIntegrityTagLength; i++) {
        tag[i] = i;
    }

    RetryPacket packet;
    packet.SetRetryIntegrityTag(tag);

    // Create 64-byte token data (0-63)
    uint8_t token_data[64];
    for (size_t i = 0; i < 64; i++) {
        token_data[i] = static_cast<uint8_t>(i);
    }
    std::shared_ptr<common::SingleBlockBuffer> token_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    uint32_t written = token_buffer->Write(token_data, 64);
    EXPECT_EQ(written, 64U) << "Failed to write 64 bytes to token buffer, only wrote " << written;
    auto token_span = token_buffer->GetSharedReadableSpan();
    EXPECT_EQ(token_span.GetLength(), 64U) << "Token span length is " << token_span.GetLength() << ", expected 64";
    packet.SetRetryToken(token_span);

    static const uint32_t s_buf_len = 256;
    // Create empty buffer for encoding packet
    std::shared_ptr<common::SingleBlockBuffer> buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(s_buf_len));
    EXPECT_TRUE(packet.Encode(buffer));

    // Decode the packet with flag (standard way)
    RetryPacket new_packet;
    bool decode_result = new_packet.DecodeWithoutCrypto(buffer, true);
    EXPECT_TRUE(decode_result) << "DecodeWithoutCrypto failed";
    
    if (!decode_result) {
        return;  // Skip remaining checks if decoding failed
    }

    auto new_tag = new_packet.GetRetryIntegrityTag();
    if (new_tag != nullptr) {
        for (uint32_t i = 0; i < kRetryIntegrityTagLength; i++) {
            EXPECT_EQ(*(new_tag + i), i) << "Tag mismatch at index " << i;
        }
    } else {
        FAIL() << "GetRetryIntegrityTag returned nullptr";
    }

    auto new_token = new_packet.GetRetryToken();
    EXPECT_NE(new_token.GetStart(), nullptr) << "GetRetryToken returned null pointer";
    EXPECT_GE(new_token.GetLength(), 64U) << "GetRetryToken returned length " << new_token.GetLength() << ", expected at least 64";
    if (new_token.GetStart() != nullptr && new_token.GetLength() >= 64) {
        for (uint32_t i = 0; i < 64; i++) {
            EXPECT_EQ(*(new_token.GetStart() + i), static_cast<uint8_t>(i)) << "Token mismatch at index " << i;
        }
    } else {
        FAIL() << "GetRetryToken returned invalid span (start=" 
               << static_cast<const void*>(new_token.GetStart()) 
               << ", length=" << new_token.GetLength() << ")";
    }
}

}
}
}