#include <gtest/gtest.h>
#include "quic/frame/max_data_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"


TEST(max_data_frame_utest, decode1) {
    quicx::MaxDataFrame frame1;
    quicx::MaxDataFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);

    frame1.SetMaximumData(23624236235626);
    frame1.Encode(buffer, alloter);
    frame2.Decode(buffer, alloter, true);

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetMaximumData(), frame2.GetMaximumData());
}