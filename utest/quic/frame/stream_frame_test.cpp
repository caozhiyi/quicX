#include <gtest/gtest.h>

#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace {

TEST(stream_frame_utest, decode1) {
    quicx::StreamFrame frame1;
    quicx::StreamFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    char frame_data[64] = "1234567890123456789012345678901234567890";
    frame1.SetFin();
    frame1.SetOffset(1042451);
    frame1.SetStreamID(20010);
    frame1.SetData((uint8_t*)frame_data, strlen(frame_data));

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetWritePair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_TRUE(frame2.HasLength());
    EXPECT_TRUE(frame2.HasOffset());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((const char*)data2, strlen(frame_data)));
}

}
}