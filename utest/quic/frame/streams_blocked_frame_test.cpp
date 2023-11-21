#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(streams_blocked_frame_utest, codec) {
    StreamsBlockedFrame frame1(FT_STREAMS_BLOCKED_BIDIRECTIONAL);
    StreamsBlockedFrame frame2(FT_STREAMS_BLOCKED_BIDIRECTIONAL);

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    frame1.SetMaximumStreams(2362423);

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