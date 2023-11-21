#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace quic {
namespace {

TEST(path_response_frame_utest, codec) {
    PathResponseFrame frame1;
    PathResponseFrame frame2;

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

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
}