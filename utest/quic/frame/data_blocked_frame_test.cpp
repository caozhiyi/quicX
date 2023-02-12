#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/data_blocked_frame.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace {

TEST(data_blocked_frame_utest, decode1) {
    quicx::DataBlockedFrame frame1;
    quicx::DataBlockedFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    frame1.SetMaximumData(23624236235626);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetWritePair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetMaximumData(), frame2.GetMaximumData());
}

}
}