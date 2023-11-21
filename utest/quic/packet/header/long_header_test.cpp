#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {
namespace {

TEST(long_header_utest, codec) {
    LongHeader header;
    header.SetVersion(1);
    uint8_t dest_id[4] = {1,2,3,4};
    header.SetDestinationConnectionId(dest_id, sizeof(dest_id));

    uint8_t src_id[4] = {5,6,7,8};
    header.SetSourceConnectionId(src_id, sizeof(src_id));

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, buf + __buf_len);

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

}
}
}