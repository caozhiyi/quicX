#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/retire_connection_id_frame.h"


TEST(retire_connection_id_frame_utest, decode1) {
    quicx::RetireConnectionIDFrame frame1;
    quicx::RetireConnectionIDFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);

    frame1.SetSequenceNumber(23624236235626);
    frame1.Encode(buffer, alloter);
    frame2.Decode(buffer, alloter, true);

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());
}