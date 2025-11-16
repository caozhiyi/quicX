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

    std::shared_ptr<common::SingleBlockBuffer> token_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(64));
    token_buffer->Write(tag, 64);
    packet.SetRetryToken(token_buffer->GetSharedReadableSpan());

    static const uint32_t s_buf_len = 256;
    // Create empty buffer for encoding packet
    std::shared_ptr<common::SingleBlockBuffer> buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(s_buf_len));
    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    RetryPacket new_packet(flag.GetFlag());
    bool decode_result = new_packet.DecodeWithoutCrypto(buffer);
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
    if (new_token.GetStart() != nullptr && new_token.GetLength() >= 64) {
        for (uint32_t i = 0; i < 64; i++) {
            EXPECT_EQ(*(new_token.GetStart() + i), i) << "Token mismatch at index " << i;
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