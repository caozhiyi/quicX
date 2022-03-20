#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/stream_data_blocked_frame.h"


TEST(stream_data_blocked_frame_utest, decode1) {
    quicx::StreamDataBlockedFrame frame1;
    quicx::StreamDataBlockedFrame frame2;

    auto IAlloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, IAlloter);

    frame1.SetStreamID(121616546);
    frame1.SetMaximumData(23624236235626);

    EXPECT_TRUE(frame1.Encode(buffer, IAlloter));
    EXPECT_TRUE(frame2.Decode(buffer, IAlloter, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetMaximumData(), frame2.GetMaximumData());
}