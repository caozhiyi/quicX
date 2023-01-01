#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer_read_write.h"
#include "quic/frame/path_challenge_frame.h"

namespace quicx {
namespace {

TEST(path_challenge_frame_utest, decode1) {
    quicx::PathChallengeFrame frame1;
    quicx::PathChallengeFrame frame2;
    std::shared_ptr<quicx::PathResponseFrame> frame3 = std::make_shared<quicx::PathResponseFrame>();

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<BufferReadWrite> read_buffer = std::make_shared<BufferReadWrite>(alloter);
    std::shared_ptr<BufferReadWrite> write_buffer = std::make_shared<BufferReadWrite>(alloter);

    frame1.MakeData();

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    frame3->SetData(frame1.GetData());

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_TRUE(frame2.CompareData(frame3));
}

}
}