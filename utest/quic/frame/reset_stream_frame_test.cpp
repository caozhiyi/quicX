#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/reset_stream_frame.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace {

TEST(reset_frame_utest, decode1) {
    quicx::ResetStreamFrame frame1;
    quicx::ResetStreamFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<quicx::Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<quicx::Buffer>(alloter);

    frame1.SetStreamID(1010101);
    frame1.SetAppErrorCode(404);
    frame1.SetFinalSize(100245123);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetWritePair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetAppErrorCode(), frame2.GetAppErrorCode());
    EXPECT_EQ(frame1.GetFinalSize(), frame2.GetFinalSize());
}

}
}