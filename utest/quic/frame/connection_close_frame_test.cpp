#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"
#include "quic/frame/connection_close_frame.h"

namespace quicx {
namespace {

TEST(connection_close_frame_utest, decode1) {
    quicx::ConnectionCloseFrame frame1;
    quicx::ConnectionCloseFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    frame1.SetErrorCode(10086);
    frame1.SetErrFrameType(0x05);
    frame1.SetReason("it is a test.");

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetErrorCode(), frame2.GetErrorCode());
    EXPECT_EQ(frame1.GetErrFrameType(), frame2.GetErrFrameType());
    EXPECT_EQ(frame1.GetReason(), frame2.GetReason());
}

}
}