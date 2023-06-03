#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer.h"
#include "quic/frame/path_challenge_frame.h"

namespace quicx {
namespace {

TEST(path_challenge_frame_utest, codec) {
    quicx::PathChallengeFrame frame1;
    quicx::PathChallengeFrame frame2;
    std::shared_ptr<quicx::PathResponseFrame> frame3 = std::make_shared<quicx::PathResponseFrame>();

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

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