#include <cstring>

#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/ack_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(AckFrameTest, codec) {
    AckFrame frame1;
    AckFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    frame1.SetAckDelay(104);
    frame1.SetLargestAck(1234);
    frame1.SetFirstAckRange(5);
    frame1.AddAckRange(3, 5);
    frame1.AddAckRange(4, 6);
    frame1.AddAckRange(2, 3);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();

    // Wire-level check: byte-exact encoding per RFC 9000 section 19.3 (varints per section 16)
    static const uint8_t expected_wire[] = {
        0x02,       // type = kAck
        0x44, 0xD2, // largest acknowledged = 1234
        0x40, 0x68, // ack delay = 104
        0x03,       // ack range count = 3
        0x05,       // first ack range = 5
        0x03, 0x05, // gap = 3, ack range = 5
        0x04, 0x06, // gap = 4, ack range = 6
        0x02, 0x03, // gap = 2, ack range = 3
    };
    ASSERT_EQ(data_span.GetLength(), sizeof(expected_wire));
    EXPECT_EQ(0, memcmp(data_span.GetStart(), expected_wire, sizeof(expected_wire)));

    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetAckDelay(), frame2.GetAckDelay());
    EXPECT_EQ(frame1.GetFirstAckRange(), frame2.GetFirstAckRange());
    EXPECT_EQ(frame1.GetLargestAck(), frame2.GetLargestAck());

    auto range = frame2.GetAckRange();
    ASSERT_EQ(range.size(), 3u);
    EXPECT_EQ(range[0].GetGap(), 3u);
    EXPECT_EQ(range[0].GetAckRangeLength(), 5u);
    EXPECT_EQ(range[1].GetGap(), 4u);
    EXPECT_EQ(range[1].GetAckRangeLength(), 6u);
    EXPECT_EQ(range[2].GetGap(), 2u);
    EXPECT_EQ(range[2].GetAckRangeLength(), 3u);
}

TEST(AckEcnFrameTest, decod1) {
    AckEcnFrame frame1;
    AckEcnFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    frame1.SetAckDelay(104);
    frame1.SetLargestAck(4321);
    frame1.SetFirstAckRange(7);
    // frame1.SetFirstAckRange(10012);
    // frame1.SetLargestAck(19);
    frame1.AddAckRange(3, 5);
    frame1.AddAckRange(4, 6);
    frame1.AddAckRange(2, 3);
    frame1.SetEct0(1009);
    frame1.SetEct1(2003);
    frame1.SetEcnCe(203);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();

    // Wire-level check: byte-exact encoding per RFC 9000 section 19.4 (ACK_ECN)
    static const uint8_t expected_wire[] = {
        0x03,       // type = kAckEcn
        0x50, 0xE1, // largest acknowledged = 4321
        0x40, 0x68, // ack delay = 104
        0x03,       // ack range count = 3
        0x07,       // first ack range = 7
        0x03, 0x05, // gap = 3, ack range = 5
        0x04, 0x06, // gap = 4, ack range = 6
        0x02, 0x03, // gap = 2, ack range = 3
        0x43, 0xF1, // ect0 = 1009
        0x47, 0xD3, // ect1 = 2003
        0x40, 0xCB, // ecn ce = 203
    };
    ASSERT_EQ(data_span.GetLength(), sizeof(expected_wire));
    EXPECT_EQ(0, memcmp(data_span.GetStart(), expected_wire, sizeof(expected_wire)));

    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetAckDelay(), frame2.GetAckDelay());
    EXPECT_EQ(frame1.GetFirstAckRange(), frame2.GetFirstAckRange());
    EXPECT_EQ(frame1.GetLargestAck(), frame2.GetLargestAck());

    auto range = frame2.GetAckRange();
    ASSERT_EQ(range.size(), 3u);
    EXPECT_EQ(range[0].GetGap(), 3u);
    EXPECT_EQ(range[0].GetAckRangeLength(), 5u);
    EXPECT_EQ(range[1].GetGap(), 4u);
    EXPECT_EQ(range[1].GetAckRangeLength(), 6u);
    EXPECT_EQ(range[2].GetGap(), 2u);
    EXPECT_EQ(range[2].GetAckRangeLength(), 3u);

    EXPECT_EQ(frame1.GetEct0(), frame2.GetEct0());
    EXPECT_EQ(frame1.GetEct1(), frame2.GetEct1());
    EXPECT_EQ(frame1.GetEcnCe(), frame2.GetEcnCe());
}

}  // namespace
}  // namespace quic
}  // namespace quicx