#include <gtest/gtest.h>

#include "quic/frame/ack_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

TEST(frame_decode_utest, decode1) {
    auto IAlloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, IAlloter);

    quicx::AckFrame ack_frame1;
    std::shared_ptr<quicx::AckFrame> ack_frame2;

    quicx::StopSendingFrame stop_frame1;
    std::shared_ptr<quicx::StopSendingFrame> stop_frame2;

    quicx::StreamFrame stream_frame1;
    std::shared_ptr<quicx::StreamFrame> stream_frame2;

    quicx::NewConnectionIDFrame new_frame1;
    std::shared_ptr<quicx::NewConnectionIDFrame> new_frame2;

    quicx::RetireConnectionIDFrame retire_frame1;
    std::shared_ptr<quicx::RetireConnectionIDFrame> retire_frame2;

    quicx::ConnectionCloseFrame close_frame1;
    std::shared_ptr<quicx::ConnectionCloseFrame> close_frame2;

    // ack frame
    ack_frame1.SetAckDelay(104);
    //ack_frame1.SetFirstAckRange(10012);
    //ack_frame1.SetLargestAck(19);
    ack_frame1.AddAckRange(3, 5);
    ack_frame1.AddAckRange(4, 6);
    ack_frame1.AddAckRange(2, 3);
    EXPECT_TRUE(ack_frame1.Encode(buffer, IAlloter));

    // stop sending frame
    stop_frame1.SetStreamID(1010101);
    stop_frame1.SetAppErrorCode(404);
    EXPECT_TRUE(stop_frame1.Encode(buffer, IAlloter));

    // stream frame
    auto data = std::make_shared<quicx::BufferQueue>(block, IAlloter);
    char frame_data[64] = "1234567890123456789012345678901234567890";
    data->Write(frame_data, sizeof(frame_data));
    stream_frame1.SetFin();
    stream_frame1.SetOffset(1042451);
    stream_frame1.SetStreamID(20010);
    stream_frame1.SetData(data);
    EXPECT_TRUE(stream_frame1.Encode(buffer, IAlloter));

    // new connection id frame
    new_frame1.SetRetirePriorTo(10086);
    new_frame1.SetSequenceNumber(2352632);
    char toekn[128] = "123456789012345678901234567890123456789801234567890";
    new_frame1.SetStatelessResetToken(toekn);
    new_frame1.AddConnectionID(1212121);
    new_frame1.AddConnectionID(1212122);
    new_frame1.AddConnectionID(1212123);
    new_frame1.AddConnectionID(1212124);
    new_frame1.AddConnectionID(1212125);
    EXPECT_TRUE(new_frame1.Encode(buffer, IAlloter));

    // retire connection id frame
    retire_frame1.SetSequenceNumber(23624236235626);
    EXPECT_TRUE(retire_frame1.Encode(buffer, IAlloter));

    // connection close frame
    close_frame1.SetErrorCode(10086);
    close_frame1.SetErrFrameType(0x05);
    close_frame1.SetReason("it is a test.");
    EXPECT_TRUE(close_frame1.Encode(buffer, IAlloter));

    // decode frames
    std::vector<std::shared_ptr<quicx::IFrame>> frames;
    EXPECT_TRUE(quicx::DecodeFrame(buffer, IAlloter, frames));
    EXPECT_EQ(frames.size(), 6);

    // check decode result
    ack_frame2 = std::dynamic_pointer_cast<quicx::AckFrame>(frames[0]);
    stop_frame2 = std::dynamic_pointer_cast<quicx::StopSendingFrame>(frames[1]);
    stream_frame2 = std::dynamic_pointer_cast<quicx::StreamFrame>(frames[2]);
    new_frame2 = std::dynamic_pointer_cast<quicx::NewConnectionIDFrame>(frames[3]);
    retire_frame2 = std::dynamic_pointer_cast<quicx::RetireConnectionIDFrame>(frames[4]);
    close_frame2 = std::dynamic_pointer_cast<quicx::ConnectionCloseFrame>(frames[5]);

    // check ack frame
    EXPECT_EQ(ack_frame1.GetType(), ack_frame2->GetType());
    EXPECT_EQ(ack_frame1.GetAckDelay(), ack_frame2->GetAckDelay());
    //EXPECT_EQ(ack_frame1.GetFirstAckRange(), ack_frame2->GetFirstAckRange());
    //EXPECT_EQ(ack_frame1.GetLargestAck(), ack_frame2->GetLargestAck());
    auto range = ack_frame2->GetAckRange();
    EXPECT_EQ(range.size(), 3);
    /*EXPECT_EQ(range[0]._gap, 3);
    EXPECT_EQ(range[0]._ack_range, 5);
    EXPECT_EQ(range[1]._gap, 4);
    EXPECT_EQ(range[1]._ack_range, 6);
    EXPECT_EQ(range[2]._gap, 2);
    EXPECT_EQ(range[2]._ack_range, 3);*/

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
    char frame_data2[64] = {0};
    data2->Read(frame_data2, data2->GetCanReadLength());
    EXPECT_EQ(std::string(frame_data), std::string(frame_data2));

    // check new connection id frame
    EXPECT_EQ(new_frame1.GetType(), new_frame2->GetType());
    EXPECT_EQ(new_frame1.GetRetirePriorTo(), new_frame2->GetRetirePriorTo());
    EXPECT_EQ(new_frame1.GetSequenceNumber(), new_frame2->GetSequenceNumber());
    EXPECT_EQ(new_frame1.GetConnectionID().size(), new_frame2->GetConnectionID().size());
    EXPECT_EQ(std::string(new_frame1.GetStatelessResetToken(), quicx::__stateless_reset_token_length), 
        std::string(new_frame2->GetStatelessResetToken(), quicx::__stateless_reset_token_length));

    // check retire connection id frame
    EXPECT_EQ(retire_frame1.GetType(), retire_frame2->GetType());
    EXPECT_EQ(retire_frame1.GetSequenceNumber(), retire_frame2->GetSequenceNumber());

    // check connection close frame
    EXPECT_EQ(close_frame1.GetType(), close_frame2->GetType());
    EXPECT_EQ(close_frame1.GetErrorCode(), close_frame2->GetErrorCode());
    EXPECT_EQ(close_frame1.GetErrFrameType(), close_frame2->GetErrFrameType());
    EXPECT_EQ(close_frame1.GetReason(), close_frame2->GetReason());
}