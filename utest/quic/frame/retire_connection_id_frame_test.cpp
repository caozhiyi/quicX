#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace {

TEST(retire_connection_id_frame_utest, decode1) {
    quicx::RetireConnectionIDFrame frame1;
    quicx::RetireConnectionIDFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<quicx::IBufferReadOnly> read_buffer = std::make_shared<quicx::BufferReadOnly>(alloter);
    std::shared_ptr<quicx::IBufferWriteOnly> write_buffer = std::make_shared<quicx::BufferWriteOnly>(alloter);

    frame1.SetSequenceNumber(23624236235626);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetAllData();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());
}

}
}