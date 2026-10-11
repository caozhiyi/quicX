#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/config.h"
#include "quic/packet/header/short_header.h"

namespace quicx {
namespace quic {
namespace {

TEST(ShortHeaderTest, codec) {
    ShortHeader header;

    // The short header does not carry the DCID length on the wire; the receiver
    // decodes with its own pre-known length (kDefaultDestinationCidLength),
    // which must equal the length of the CIDs quicX issues (8). The round trip
    // therefore only closes when encoding with that same length.
    uint8_t dest_id[kDefaultDestinationCidLength] = {1, 2, 3, 4, 5, 6, 7, 8};
    header.SetDestinationConnectionId(dest_id, sizeof(dest_id));

    // See long_header_test for why the buffer starts empty (append
    // semantics required by packet coalescing).
    static const uint8_t s_buf_len = 128;
    std::shared_ptr<common::SingleBlockBuffer> buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(s_buf_len));

    EXPECT_TRUE(header.EncodeHeader(buffer));

    ShortHeader new_header;
    EXPECT_TRUE(new_header.DecodeHeader(buffer, true));

    EXPECT_EQ(new_header.GetDestinationConnectionIdLength(), header.GetDestinationConnectionIdLength());
    const uint8_t* new_dest_id = new_header.GetDestinationConnectionId();
    for (size_t i = 0; i < sizeof(dest_id); i++) {
        EXPECT_EQ(*(new_dest_id + i), dest_id[i]);
    }
}

TEST(ShortHeaderTest, codec_explicit_length) {
    // Codec capability check: the encode/decode pair itself is length-agnostic
    // and supports the full RFC 9000 §7.2 range (0..20) as long as the decoding
    // header is told the length out of band, as the runtime always is.
    uint8_t dest_id[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

    ShortHeader header;
    header.SetDestinationConnectionId(dest_id, sizeof(dest_id));

    static const uint8_t s_buf_len = 128;
    std::shared_ptr<common::SingleBlockBuffer> buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(s_buf_len));

    EXPECT_TRUE(header.EncodeHeader(buffer));

    ShortHeader new_header;
    // Explicitly arm the decoder with the same out-of-band length (nullptr:
    // keep the decoded bytes, only set the length).
    new_header.SetDestinationConnectionId(nullptr, sizeof(dest_id));
    EXPECT_TRUE(new_header.DecodeHeader(buffer, true));

    EXPECT_EQ(new_header.GetDestinationConnectionIdLength(), sizeof(dest_id));
    const uint8_t* new_dest_id = new_header.GetDestinationConnectionId();
    for (size_t i = 0; i < sizeof(dest_id); i++) {
        EXPECT_EQ(*(new_dest_id + i), dest_id[i]);
    }
}

}  // namespace
}  // namespace quic
}  // namespace quicx