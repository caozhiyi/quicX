#include <gtest/gtest.h>

#include "quic/frame/ack_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/stop_sending_frame.h"
#include "common/buffer/buffer.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(frame_decode_utest, codec) {
    auto alloter = common::MakeBlockMemoryPoolPtr(4096, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    AckFrame ack_frame1;
    std::shared_ptr<AckFrame> ack_frame2;

    StopSendingFrame stop_frame1;
    std::shared_ptr<StopSendingFrame> stop_frame2;

    StreamFrame stream_frame1;
    std::shared_ptr<StreamFrame> stream_frame2;

    NewConnectionIDFrame new_frame1;
    std::shared_ptr<NewConnectionIDFrame> new_frame2;

    RetireConnectionIDFrame retire_frame1;
    std::shared_ptr<RetireConnectionIDFrame> retire_frame2;

    ConnectionCloseFrame close_frame1;
    std::shared_ptr<ConnectionCloseFrame> close_frame2;

    // ack frame
    ack_frame1.SetAckDelay(104);
    //ack_frame1.SetFirstAckRange(10012);
    //ack_frame1.SetLargestAck(19);
    ack_frame1.AddAckRange(3, 5);
    ack_frame1.AddAckRange(4, 6);
    ack_frame1.AddAckRange(2, 3);
    EXPECT_TRUE(ack_frame1.Encode(write_buffer));

    // stop sending frame
    stop_frame1.SetStreamID(1010101);
    stop_frame1.SetAppErrorCode(404);
    EXPECT_TRUE(stop_frame1.Encode(write_buffer));

    // stream frame
    char frame_data[64] = "1234567890123456789012345678901234567890";
    stream_frame1.SetFin();
    stream_frame1.SetOffset(1042451);
    stream_frame1.SetStreamID(20010);
    stream_frame1.SetData((uint8_t*)frame_data, strlen(frame_data));
    EXPECT_TRUE(stream_frame1.Encode(write_buffer));

    // new connection id frame
    new_frame1.SetRetirePriorTo(10086);
    new_frame1.SetSequenceNumber(2352632);
    char token[128] = "123456789012345678901234567890123456789801234567890";
    new_frame1.SetStatelessResetToken((uint8_t*)token);
    EXPECT_TRUE(new_frame1.Encode(write_buffer));

    // retire connection id frame
    retire_frame1.SetSequenceNumber(23624236235626);
    EXPECT_TRUE(retire_frame1.Encode(write_buffer));

    // connection close frame
    close_frame1.SetErrorCode(10086);
    close_frame1.SetErrFrameType(0x05);
    close_frame1.SetReason("it is a test.");
    EXPECT_TRUE(close_frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());

    // decode frames
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    EXPECT_EQ(frames.size(), 6);

    // check decode result
    ack_frame2 = std::dynamic_pointer_cast<AckFrame>(frames[0]);
    stop_frame2 = std::dynamic_pointer_cast<StopSendingFrame>(frames[1]);
    stream_frame2 = std::dynamic_pointer_cast<StreamFrame>(frames[2]);
    new_frame2 = std::dynamic_pointer_cast<NewConnectionIDFrame>(frames[3]);
    retire_frame2 = std::dynamic_pointer_cast<RetireConnectionIDFrame>(frames[4]);
    close_frame2 = std::dynamic_pointer_cast<ConnectionCloseFrame>(frames[5]);

    // check ack frame
    EXPECT_EQ(ack_frame1.GetType(), ack_frame2->GetType());
    EXPECT_EQ(ack_frame1.GetAckDelay(), ack_frame2->GetAckDelay());
    //EXPECT_EQ(ack_frame1.GetFirstAckRange(), ack_frame2->GetFirstAckRange());
    //EXPECT_EQ(ack_frame1.GetLargestAck(), ack_frame2->GetLargestAck());
    auto range = ack_frame2->GetAckRange();
    EXPECT_EQ(range.size(), 3);
    /*EXPECT_EQ(range[0].gap_, 3);
    EXPECT_EQ(range[0].ack_range_, 5);
    EXPECT_EQ(range[1].gap_, 4);
    EXPECT_EQ(range[1].ack_range_, 6);
    EXPECT_EQ(range[2].gap_, 2);
    EXPECT_EQ(range[2].ack_range_, 3);*/

    // check sending frame
    EXPECT_EQ(stop_frame1.GetType(), stop_frame2->GetType());
    EXPECT_EQ(stop_frame1.GetStreamID(), stop_frame2->GetStreamID());
    EXPECT_EQ(stop_frame1.GetAppErrorCode(), stop_frame2->GetAppErrorCode());

    // check stream frame
    EXPECT_EQ(stream_frame1.GetType(), stream_frame2->GetType());
    EXPECT_TRUE(stream_frame2->HasLength());
    EXPECT_TRUE(stream_frame2->HasOffset());
    EXPECT_EQ(stream_frame1.GetStreamID(), stream_frame2->GetStreamID());
    EXPECT_EQ(stream_frame1.GetOffset(), stream_frame2->GetOffset());
    auto data2 = stream_frame2->GetData();
    EXPECT_EQ(std::string(frame_data), std::string((char*)data2, stream_frame2->GetLength()));

    // check new connection id frame
    EXPECT_EQ(new_frame1.GetType(), new_frame2->GetType());
    EXPECT_EQ(new_frame1.GetRetirePriorTo(), new_frame2->GetRetirePriorTo());
    EXPECT_EQ(new_frame1.GetSequenceNumber(), new_frame2->GetSequenceNumber());
    EXPECT_EQ(std::string((char*)new_frame1.GetStatelessResetToken(), kStatelessResetTokenLength), 
        std::string((char*)new_frame2->GetStatelessResetToken(), kStatelessResetTokenLength));

    // check retire connection id frame
    EXPECT_EQ(retire_frame1.GetType(), retire_frame2->GetType());
    EXPECT_EQ(retire_frame1.GetSequenceNumber(), retire_frame2->GetSequenceNumber());

    // check connection close frame
    EXPECT_EQ(close_frame1.GetType(), close_frame2->GetType());
    EXPECT_EQ(close_frame1.GetErrorCode(), close_frame2->GetErrorCode());
    EXPECT_EQ(close_frame1.GetErrFrameType(), close_frame2->GetErrFrameType());
    EXPECT_EQ(close_frame1.GetReason(), close_frame2->GetReason());
}

}
}
}