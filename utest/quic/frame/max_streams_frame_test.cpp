#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/max_streams_frame.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace quic {
namespace {

TEST(max_streams_frame_utest, codec) {
    MaxStreamsFrame frame1(FrameType::kMaxStreamsBidirectional);
    MaxStreamsFrame frame2(FrameType::kMaxStreamsBidirectional);

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    frame1.SetMaximumStreams(23624236235626);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetMaximumStreams(), frame2.GetMaximumStreams());
}

}
}
}