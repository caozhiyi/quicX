#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer.h"
#include "quic/frame/path_challenge_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(path_challenge_frame_utest, codec) {
    PathChallengeFrame frame1;
    PathChallengeFrame frame2;
    std::shared_ptr<PathResponseFrame> frame3 = std::make_shared<PathResponseFrame>();

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    frame1.MakeData();

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    frame3->SetData(frame1.GetData());

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_TRUE(frame2.CompareData(frame3));
}

}
}
}