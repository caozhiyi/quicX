#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/reset_stream_frame.h"


TEST(reset_frame_utest, decode1) {
    quicx::ResetStreamFrame frame1;
    quicx::ResetStreamFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);

    frame1.SetStreamID(1010101);
    frame1.SetAppErrorCode(404);
    frame1.SetFinalSize(100245123);

    EXPECT_TRUE(frame1.Encode(buffer, alloter));
    EXPECT_TRUE(frame2.Decode(buffer, alloter, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetAppErrorCode(), frame2.GetAppErrorCode());
    EXPECT_EQ(frame1.GetFinalSize(), frame2.GetFinalSize());
}