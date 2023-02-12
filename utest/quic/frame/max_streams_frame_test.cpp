#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/max_streams_frame.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace {

TEST(max_streams_frame_utest, decode1) {
    quicx::MaxStreamsFrame frame1(quicx::FT_MAX_STREAMS_BIDIRECTIONAL);
    quicx::MaxStreamsFrame frame2(quicx::FT_MAX_STREAMS_BIDIRECTIONAL);

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

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