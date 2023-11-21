#include <gtest/gtest.h>

#include "quic/frame/ack_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace quic {
namespace {

TEST(ack_frame_utest, codec) {
    AckFrame frame1;
    AckFrame frame2;

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    frame1.SetAckDelay(104);
    frame1.AddAckRange(3, 5);
    frame1.AddAckRange(4, 6);
    frame1.AddAckRange(2, 3);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
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
    AckEcnFrame frame1;
    AckEcnFrame frame2;

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

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

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
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
}