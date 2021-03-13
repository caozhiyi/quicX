#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/path_challenge_frame.h"

TEST(path_challenge_frame_utest, decode1) {
    quicx::PathChallengeFrame frame1;
    quicx::PathChallengeFrame frame2;
    std::shared_ptr<quicx::PathResponseFrame> frame3 = std::make_shared<quicx::PathResponseFrame>();

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);

    frame1.MakeData();

    EXPECT_TRUE(frame1.Encode(buffer, alloter));
    EXPECT_TRUE(frame2.Decode(buffer, alloter, true));

    frame3->SetData(frame1.GetData());

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_TRUE(frame2.CompareData(frame3));
}