#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"
#include "quic/frame/new_connection_id_frame.h"

namespace quicx {
namespace {

TEST(new_connection_id_frame_utest, codec) {
    quicx::NewConnectionIDFrame frame1;
    quicx::NewConnectionIDFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(256, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    frame1.SetRetirePriorTo(10086);
    frame1.SetSequenceNumber(2352632);

    char toekn[128] = "123456789012345678901234567890123456789801234567890";
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

    uint8_t* cid_ptr = frame2.GetConnectionID();
    uint8_t cid_len = frame2.GetConnectionIDLength();
    EXPECT_EQ(std::string(cid_ptr, cid_len), std::string(cid, len));
    EXPECT_EQ(std::string((char*)frame1.GetStatelessResetToken(), quicx::__stateless_reset_token_length), 
        std::string((char*)frame2.GetStatelessResetToken(), quicx::__stateless_reset_token_length));
}

}
}