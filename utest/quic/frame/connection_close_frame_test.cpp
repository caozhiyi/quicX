#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/connection_close_frame.h"

TEST(connection_close_frame_utest, decode1) {
    quicx::ConnectionCloseFrame frame1;
    quicx::ConnectionCloseFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);

    frame1.SetErrorCode(10086);
    frame1.SetErrFrameType(0x05);
    frame1.SetReason("it is a test.");
    frame1.Encode(buffer, alloter);
    frame2.Decode(buffer, alloter, true);

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetErrorCode(), frame2.GetErrorCode());
    EXPECT_EQ(frame1.GetErrFrameType(), frame2.GetErrFrameType());
    EXPECT_EQ(frame1.GetReason(), frame2.GetReason());
}