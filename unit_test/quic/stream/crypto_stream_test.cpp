#include <gtest/gtest.h>
#include "quic/frame/crypto_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/stream/crypto_stream.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/single_block_buffer.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
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
    std::shared_ptr<common::BlockMemoryPool> alloter = common::MakeBlockMemoryPoolPtr(1024, 5);
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(alloter, event_loop, nullptr, nullptr, nullptr);

    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    std::shared_ptr<CryptoFrame> frame1 = std::make_shared<CryptoFrame>();
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer->Write(data, 5);
    frame1->SetData(data_buffer->GetSharedReadableSpan());
    frame1->SetOffset(0);

    std::shared_ptr<CryptoFrame> frame2 = std::make_shared<CryptoFrame>();
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer2->Write(data + 5, 10);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    frame2->SetOffset(5);

    std::shared_ptr<CryptoFrame> frame3 = std::make_shared<CryptoFrame>();
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(15));
    data_buffer3->Write(data + 15, 15);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    frame3->SetOffset(15);

    uint8_t recv_data[50] = {0};
    uint32_t recv_size = 0;
    stream->SetStreamReadCallBack([&recv_data, &recv_size](std::shared_ptr<IBufferRead> buffer, bool is_last, uint32_t err){
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
    std::shared_ptr<common::BlockMemoryPool> alloter = common::MakeBlockMemoryPoolPtr(1024, 5);
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(alloter, event_loop, nullptr, nullptr, nullptr);

    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    stream->Send(data, 5, kInitial);
    stream->Send(data + 5, 10, kHandshake);
    stream->Send(data + 15, 15, kApplication);

    FixBufferFrameVisitor frame_visitor(1450);
    EXPECT_EQ(stream->TrySendData(&frame_visitor), IStream::TrySendResult::kBreak);
    EXPECT_EQ(stream->TrySendData(&frame_visitor), IStream::TrySendResult::kBreak);
    EXPECT_EQ(stream->TrySendData(&frame_visitor), IStream::TrySendResult::kSuccess);

    std::vector<std::shared_ptr<IFrame>> frames;
    bool decode_result = DecodeFrames(frame_visitor.GetBuffer(), frames);
    EXPECT_TRUE(decode_result) << "DecodeFrames failed, decoded " << frames.size() << " frames";
    EXPECT_EQ(frames.size(), 3) << "Expected 3 frames but got " << frames.size();
    
    // Only proceed with checks if we successfully decoded all frames
    if (!decode_result || frames.size() != 3) {
        return;  // Skip remaining checks if decoding failed
    }

    // Verify all casts succeeded before accessing
    auto frame0 = std::dynamic_pointer_cast<CryptoFrame>(frames[0]);
    auto frame1 = std::dynamic_pointer_cast<CryptoFrame>(frames[1]);
    auto frame2 = std::dynamic_pointer_cast<CryptoFrame>(frames[2]);
    
    EXPECT_NE(frame0, nullptr) << "Failed to cast frame[0] to CryptoFrame";
    EXPECT_NE(frame1, nullptr) << "Failed to cast frame[1] to CryptoFrame";
    EXPECT_NE(frame2, nullptr) << "Failed to cast frame[2] to CryptoFrame";
    
    // Skip remaining checks if any cast failed
    if (!frame0 || !frame1 || !frame2) {
        return;
    }

    EXPECT_EQ(frame0->GetOffset(), 0);
    EXPECT_EQ(frame1->GetOffset(), 5);
    EXPECT_EQ(frame2->GetOffset(), 15);

    EXPECT_EQ(frame0->GetLength(), 5);
    EXPECT_EQ(frame1->GetLength(), 10);
    EXPECT_EQ(frame2->GetLength(), 15);

    EXPECT_TRUE(Check(frame0->GetData().GetStart(), data, 5));
    EXPECT_TRUE(Check(frame1->GetData().GetStart(), data + 5, 10));
    EXPECT_TRUE(Check(frame2->GetData().GetStart(), data + 15, 15));
}

}
}
}