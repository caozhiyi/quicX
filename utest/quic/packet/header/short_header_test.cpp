#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/packet/header/short_header.h"

namespace quicx {
namespace quic {
namespace {

TEST(short_header_utest, codec) {
    ShortHeader header;

    uint8_t dest_id[20] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    header.SetDestinationConnectionId(dest_id, sizeof(dest_id));

    static const uint8_t s_buf_len = 128;
    uint8_t buf[s_buf_len] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, buf + s_buf_len);

    EXPECT_TRUE(header.EncodeHeader(buffer));

    ShortHeader new_header;
    EXPECT_TRUE(new_header.DecodeHeader(buffer, true));


    EXPECT_EQ(new_header.GetDestinationConnectionIdLength(), header.GetDestinationConnectionIdLength());
    const uint8_t* new_dest_id = new_header.GetDestinationConnectionId();
    for (size_t i = 0; i < sizeof(dest_id); i++) {
        EXPECT_EQ(*(new_dest_id + i), dest_id[i]);
    }
}

}
}
}