#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {
namespace {

TEST(LongHeaderTest, codec) {
    LongHeader header;
    header.SetVersion(1);
    uint8_t dest_id[4] = {1, 2, 3, 4};
    header.SetDestinationConnectionId(dest_id, sizeof(dest_id));

    uint8_t src_id[4] = {5, 6, 7, 8};
    header.SetSourceConnectionId(src_id, sizeof(src_id));

    // EncodeFlag/EncodeHeader now have *append* semantics (required by
    // RFC 9000 §12.2 packet coalescing). The buffer therefore starts
    // empty — previously this test pre-filled the buffer relying on a
    // Clear() side-effect inside EncodeFlag that has since been removed.
    static const uint8_t s_buf_len = 128;
    std::shared_ptr<common::SingleBlockBuffer> buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(s_buf_len));

    EXPECT_TRUE(header.EncodeHeader(buffer));

    LongHeader new_header;
    EXPECT_TRUE(new_header.DecodeHeader(buffer, true));

    EXPECT_EQ(new_header.GetSourceConnectionIdLength(), header.GetSourceConnectionIdLength());
    const uint8_t* new_src_id = new_header.GetSourceConnectionId();
    for (size_t i = 0; i < sizeof(src_id); i++) {
        EXPECT_EQ(*(new_src_id + i), src_id[i]);
    }

    EXPECT_EQ(new_header.GetDestinationConnectionIdLength(), header.GetDestinationConnectionIdLength());
    const uint8_t* new_dest_id = new_header.GetDestinationConnectionId();
    for (size_t i = 0; i < sizeof(dest_id); i++) {
        EXPECT_EQ(*(new_dest_id + i), dest_id[i]);
    }
}

}  // namespace
}  // namespace quic
}  // namespace quicx