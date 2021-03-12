#include <gtest/gtest.h>

#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"

TEST(stream_frame_utest, decode1) {
    quicx::StreamFrame frame1;
    quicx::StreamFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);
    auto data = std::make_shared<quicx::BufferQueue>(block, alloter);

    char frame_data[64] = "1234567890123456789012345678901234567890";
    data->Write(frame_data, sizeof(frame_data));
    frame1.SetFin();
    frame1.SetOffset(1042451);
    frame1.SetStreamID(20010);
    frame1.SetData(data);

    frame1.Encode(buffer, alloter);
    frame2.Decode(buffer, alloter, true);

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_TRUE(frame2.HasLength());
    EXPECT_TRUE(frame2.HasOffset());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    char frame_data2[64] = {0};
    data2->Read(frame_data2, data2->GetCanReadLength());
    EXPECT_EQ(std::string(frame_data), std::string(frame_data2));
}