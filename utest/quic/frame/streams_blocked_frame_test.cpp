#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_write.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {
namespace {

TEST(streams_blocked_frame_utest, decode1) {
    quicx::StreamsBlockedFrame frame1(quicx::FT_STREAMS_BLOCKED_BIDIRECTIONAL);
    quicx::StreamsBlockedFrame frame2(quicx::FT_STREAMS_BLOCKED_BIDIRECTIONAL);

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<BufferReadWrite> read_buffer = std::make_shared<BufferReadWrite>(alloter);
    std::shared_ptr<BufferReadWrite> write_buffer = std::make_shared<BufferReadWrite>(alloter);

    frame1.SetMaximumStreams(2362423);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetMaximumStreams(), frame2.GetMaximumStreams());
}

}
}