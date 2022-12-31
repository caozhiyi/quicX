#include <gtest/gtest.h>

#include "quic/frame/ack_frame.h"
#include "common/alloter/pool_block.h"

namespace quicx {
namespace {

TEST(ack_frame_utest, decode1) {
    quicx::AckFrame frame1;
    quicx::AckFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<quicx::IBufferReadOnly> read_buffer = std::make_shared<quicx::BufferReadOnly>(alloter);
    std::shared_ptr<quicx::IBufferWriteOnly> write_buffer = std::make_shared<quicx::BufferWriteOnly>(alloter);

    frame1.SetAckDelay(104);
    frame1.AddAckRange(3, 5);
    frame1.AddAckRange(4, 6);
    frame1.AddAckRange(2, 3);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetAllData();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetAckDelay(), frame2.GetAckDelay());
    //EXPECT_EQ(frame1.GetFirstAckRange(), frame2.GetFirstAckRange());
    //EXPECT_EQ(frame1.GetLargestAck(), frame2.GetLargestAck());

    auto range = frame2.GetAckRange();
    EXPECT_EQ(range.size(), 3);
    /*EXPECT_EQ(range[0]._gap, 3);
    EXPECT_EQ(range[0]._ack_range, 5);
    EXPECT_EQ(range[1]._gap, 4);
    EXPECT_EQ(range[1]._ack_range, 6);
    EXPECT_EQ(range[2]._gap, 2);
    EXPECT_EQ(range[2]._ack_range, 3);*/
}

TEST(ack_ecn_frame_utest, decod1) {
    quicx::AckEcnFrame frame1;
    quicx::AckEcnFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<quicx::IBufferReadOnly> read_buffer = std::make_shared<quicx::BufferReadOnly>(alloter);
    std::shared_ptr<quicx::IBufferWriteOnly> write_buffer = std::make_shared<quicx::BufferWriteOnly>(alloter);

    frame1.SetAckDelay(104);
    //frame1.SetFirstAckRange(10012);
    //frame1.SetLargestAck(19);
    frame1.AddAckRange(3, 5);
    frame1.AddAckRange(4, 6);
    frame1.AddAckRange(2, 3);
    frame1.SetEct0(1009);
    frame1.SetEct1(2003);
    frame1.SetEcnCe(203);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetAllData();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetAckDelay(), frame2.GetAckDelay());
    //EXPECT_EQ(frame1.GetFirstAckRange(), frame2.GetFirstAckRange());
    //EXPECT_EQ(frame1.GetLargestAck(), frame2.GetLargestAck());

    auto range = frame2.GetAckRange();
    EXPECT_EQ(range.size(), 3);
    /*EXPECT_EQ(range[0]._gap, 3);
    EXPECT_EQ(range[0]._ack_range, 5);
    EXPECT_EQ(range[1]._gap, 4);
    EXPECT_EQ(range[1]._ack_range, 6);
    EXPECT_EQ(range[2]._gap, 2);
    EXPECT_EQ(range[2]._ack_range, 3);*/

    EXPECT_EQ(frame1.GetEct0(), frame2.GetEct0());
    EXPECT_EQ(frame1.GetEct1(), frame2.GetEct1());
    EXPECT_EQ(frame1.GetEcnCe(), frame2.GetEcnCe());
}

}
}