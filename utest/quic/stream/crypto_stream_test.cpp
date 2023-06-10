#include <gtest/gtest.h>
#include "quic/frame/crypto_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/stream/crypto_stream.h"
#include "common/alloter/pool_block.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {
namespace {

bool Check(uint8_t* data1, uint8_t* data2, uint32_t len) {
    for (size_t i = 0; i < len; i++) {
        if (*(data1 + i) != *(data2 + i)) {
            return false;
        }
    }
    return true;
}

TEST(crypto_stream_utest, recv) {
    std::shared_ptr<BlockMemoryPool> alloter = MakeBlockMemoryPoolPtr(1024, 5);
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(alloter);

    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    std::shared_ptr<CryptoFrame> frame1 = std::make_shared<CryptoFrame>();
    frame1->SetData(data, 5);
    frame1->SetOffset(0);

    std::shared_ptr<CryptoFrame> frame2 = std::make_shared<CryptoFrame>();
    frame2->SetData(data + 5, 10);
    frame2->SetOffset(5);

    std::shared_ptr<CryptoFrame> frame3 = std::make_shared<CryptoFrame>();
    frame3->SetData(data + 15, 15);
    frame3->SetOffset(15);

    uint8_t recv_data[50] = {0};
    uint32_t recv_size = 0;
    stream->SetRecvCallBack([&recv_data, &recv_size](std::shared_ptr<IBufferChains> buffer, int32_t err){
        EXPECT_EQ(err, 0);
        recv_size += buffer->Read(recv_data + recv_size, 50);
    });

    stream->OnFrame(frame2);
    stream->OnFrame(frame1);
    stream->OnFrame(frame3);
    EXPECT_TRUE(Check(recv_data, data, recv_size));

    recv_size = 0;
    stream->OnFrame(frame2);
    stream->OnFrame(frame3);
    stream->OnFrame(frame1);
    EXPECT_TRUE(Check(recv_data, data, recv_size));

    recv_size = 0;
    stream->OnFrame(frame1);
    stream->OnFrame(frame3);
    stream->OnFrame(frame2);
    EXPECT_TRUE(Check(recv_data, data, recv_size));

    recv_size = 0;
    stream->OnFrame(frame1);
    stream->OnFrame(frame3);
    stream->OnFrame(frame3);
    stream->OnFrame(frame2);
    EXPECT_TRUE(Check(recv_data, data, recv_size));

    recv_size = 0;
    stream->OnFrame(frame2);
    stream->OnFrame(frame3);
    stream->OnFrame(frame2);
    stream->OnFrame(frame1);
    EXPECT_TRUE(Check(recv_data, data, recv_size));
}

TEST(crypto_stream_utest, send) {
    std::shared_ptr<BlockMemoryPool> alloter = MakeBlockMemoryPoolPtr(1024, 5);
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(alloter);

    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    stream->Send(data, 5, EL_INITIAL);
    stream->Send(data + 5, 10, EL_HANDSHAKE);
    stream->Send(data + 15, 15, EL_APPLICATION);

    FixBufferFrameVisitor frame_visitor(1450);
    EXPECT_EQ(stream->TrySendData(&frame_visitor), IStream::TSR_BREAK);
    EXPECT_EQ(stream->TrySendData(&frame_visitor), IStream::TSR_BREAK);
    EXPECT_EQ(stream->TrySendData(&frame_visitor), IStream::TSR_SUCCESS);

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(frame_visitor.GetBuffer(), frames));

    EXPECT_EQ(frames.size(), 3);
    EXPECT_EQ(std::dynamic_pointer_cast<CryptoFrame>(frames[0])->GetOffset(), 0);
    EXPECT_EQ(std::dynamic_pointer_cast<CryptoFrame>(frames[1])->GetOffset(), 5);
    EXPECT_EQ(std::dynamic_pointer_cast<CryptoFrame>(frames[2])->GetOffset(), 15);

    EXPECT_EQ(std::dynamic_pointer_cast<CryptoFrame>(frames[0])->GetLength(), 5);
    EXPECT_EQ(std::dynamic_pointer_cast<CryptoFrame>(frames[1])->GetLength(), 10);
    EXPECT_EQ(std::dynamic_pointer_cast<CryptoFrame>(frames[2])->GetLength(), 15);

    EXPECT_TRUE(Check(std::dynamic_pointer_cast<CryptoFrame>(frames[0])->GetData(), data, 5));
    EXPECT_TRUE(Check(std::dynamic_pointer_cast<CryptoFrame>(frames[1])->GetData(), data + 5, 10));
    EXPECT_TRUE(Check(std::dynamic_pointer_cast<CryptoFrame>(frames[2])->GetData(), data + 15, 15));
}

}
}