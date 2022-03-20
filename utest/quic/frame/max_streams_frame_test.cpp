#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/max_streams_frame.h"


TEST(max_streams_frame_utest, decode1) {
    quicx::MaxStreamsFrame frame1(quicx::FT_MAX_STREAMS_BIDIRECTIONAL);
    quicx::MaxStreamsFrame frame2(quicx::FT_MAX_STREAMS_BIDIRECTIONAL);

    auto IAlloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, IAlloter);

    frame1.SetMaximumStreams(23624236235626);

    EXPECT_TRUE(frame1.Encode(buffer, IAlloter));
    EXPECT_TRUE(frame2.Decode(buffer, IAlloter, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetMaximumStreams(), frame2.GetMaximumStreams());
}