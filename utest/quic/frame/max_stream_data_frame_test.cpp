#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "common/buffer/buffer_read_write.h"
#include "quic/frame/max_stream_data_frame.h"

namespace quicx {
namespace {

TEST(max_stream_data_frame_utest, decode1) {
    quicx::MaxStreamDataFrame frame1;
    quicx::MaxStreamDataFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<BufferReadWrite> read_buffer = std::make_shared<BufferReadWrite>(alloter);
    std::shared_ptr<BufferReadWrite> write_buffer = std::make_shared<BufferReadWrite>(alloter);

    frame1.SetStreamID(1235125324234);
    frame1.SetMaximumData(23624236235626);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetMaximumData(), frame2.GetMaximumData());
}

}
}