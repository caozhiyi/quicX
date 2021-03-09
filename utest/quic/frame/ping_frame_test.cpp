#include <gtest/gtest.h>
#include "quic/frame/ping_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"


TEST(ping_frame_utest, decode1) {
    quicx::PingFrame frame1;
    quicx::PingFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);

    frame1.Encode(buffer, alloter);
    frame2.Decode(buffer, alloter);

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
}