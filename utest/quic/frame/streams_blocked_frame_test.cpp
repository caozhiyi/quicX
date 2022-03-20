#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/streams_blocked_frame.h"


TEST(streams_blocked_frame_utest, decode1) {
    quicx::StreamsBlockedFrame frame1(quicx::FT_STREAMS_BLOCKED_BIDIRECTIONAL);
    quicx::StreamsBlockedFrame frame2(quicx::FT_STREAMS_BLOCKED_BIDIRECTIONAL);

    auto IAlloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, IAlloter);

    frame1.SetMaximumStreams(2362423);

    EXPECT_TRUE(frame1.Encode(buffer, IAlloter));
    EXPECT_TRUE(frame2.Decode(buffer, IAlloter, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetMaximumStreams(), frame2.GetMaximumStreams());
}