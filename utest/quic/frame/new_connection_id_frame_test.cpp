#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"
#include "quic/frame/new_connection_id_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(new_connection_id_frame_utest, codec) {
    NewConnectionIDFrame frame1;
    NewConnectionIDFrame frame2;

    auto alloter = common::MakeBlockMemoryPoolPtr(256, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    frame1.SetRetirePriorTo(10086);
    frame1.SetSequenceNumber(2352632);

    uint8_t toekn[128] = {0};
    for (size_t i = 0; i < 128; i++) {
        toekn[i] = i;
    }
    
    frame1.SetStatelessResetToken((uint8_t*)toekn);

    uint8_t cid[] = {1,2,3,4,5,6,7,8};
    uint8_t len = sizeof(cid);
    frame1.SetConnectionID(cid, len);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetRetirePriorTo(), frame2.GetRetirePriorTo());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());

    uint8_t cid_ptr[20] = {};
    uint8_t cid_len = 20;
    frame2.GetConnectionID(cid_ptr, cid_len);
    EXPECT_EQ(std::string((char*)cid_ptr, cid_len), std::string((char*)cid, len));
    EXPECT_EQ(std::string((char*)frame1.GetStatelessResetToken(), kStatelessResetTokenLength), 
        std::string((char*)frame2.GetStatelessResetToken(), kStatelessResetTokenLength));
}

}
}
}