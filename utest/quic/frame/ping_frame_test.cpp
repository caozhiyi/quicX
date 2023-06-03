#include <gtest/gtest.h>

#include "quic/frame/ping_frame.h"
#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace {

TEST(ping_frame_utest, codec) {
    quicx::PingFrame frame1;
    quicx::PingFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
}

}
}