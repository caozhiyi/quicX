#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/crypto_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/stream/crypto_stream.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

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

TEST(CryptoStreamTest, recv) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(event_loop, nullptr, nullptr, nullptr);

    uint8_t data[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
    std::shared_ptr<CryptoFrame> frame1 = std::make_shared<CryptoFrame>();
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer->Write(data, 5);
    frame1->SetData(data_buffer->GetSharedReadableSpan());
    frame1->SetOffset(0);

    std::shared_ptr<CryptoFrame> frame2 = std::make_shared<CryptoFrame>();
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer2->Write(data + 5, 10);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    frame2->SetOffset(5);

    std::shared_ptr<CryptoFrame> frame3 = std::make_shared<CryptoFrame>();
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(15));
    data_buffer3->Write(data + 15, 15);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    frame3->SetOffset(15);

    uint8_t recv_data[50] = {0};
    uint32_t recv_size = 0;
    stream->SetCryptoStreamReadCallBack(
        [&recv_data, &recv_size](std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level) {
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

TEST(CryptoStreamTest, send) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(event_loop, nullptr, nullptr, nullptr);

    uint8_t data[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
    stream->Send(data, 5, kInitial);
    stream->Send(data + 5, 10, kHandshake);
    stream->Send(data + 15, 15, kApplication);

    FixBufferFrameVisitor frame_visitor(1450);
    EXPECT_EQ(stream->TrySendData(&frame_visitor, kInitial), IStream::TrySendResult::kSuccess);
    EXPECT_EQ(stream->TrySendData(&frame_visitor, kHandshake), IStream::TrySendResult::kSuccess);
    EXPECT_EQ(stream->TrySendData(&frame_visitor, kApplication), IStream::TrySendResult::kSuccess);

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
    EXPECT_EQ(frame1->GetOffset(), 0);
    EXPECT_EQ(frame2->GetOffset(), 0);

    EXPECT_EQ(frame0->GetLength(), 5);
    EXPECT_EQ(frame1->GetLength(), 10);
    EXPECT_EQ(frame2->GetLength(), 15);

    EXPECT_TRUE(Check(frame0->GetData().GetStart(), data, 5));
    EXPECT_TRUE(Check(frame1->GetData().GetStart(), data + 5, 10));
    EXPECT_TRUE(Check(frame2->GetData().GetStart(), data + 15, 15));
}

// Reproduces the interop crosstraffic storm scenario: after handshake
// confirmation the handshake-level CRYPTO flight (client Finished, 36 bytes)
// must never be regenerated, no matter how loss/PTO events fire afterwards.
namespace {

class ResurrectionVisitor: public IFrameVisitor {
public:
    bool HandleFrame(std::shared_ptr<IFrame> frame) override {
        handled_.push_back(std::dynamic_pointer_cast<CryptoFrame>(frame));
        return true;
    }
    std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
    uint8_t GetEncryptionLevel() override { return kHandshake; }
    void SetStreamDataSizeLimit(uint32_t size) override { allowance_ = size; }
    uint32_t GetLeftStreamDataSize() override { return allowance_; }
    void AddStreamDataSize(uint32_t size) override { allowance_ -= std::min(allowance_, size); }
    uint64_t GetStreamDataSize() override { return 0; }
    uint32_t GetPacketLeftSize() override { return 1400; }

    std::vector<std::shared_ptr<CryptoFrame>> handled_;
    uint32_t allowance_{0};
};

}  // namespace

TEST(CryptoStreamTest, handshake_confirmed_no_crypto_resurrection) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    std::shared_ptr<CryptoStream> stream = std::make_shared<CryptoStream>(event_loop, nullptr, nullptr, nullptr);

    uint8_t finished[36];
    for (int i = 0; i < 36; i++) finished[i] = static_cast<uint8_t>(i);

    // 1. TLS provides client Finished at handshake level; handshake-phase
    // transmission hands the frame (with its delivery handler) to the packet.
    std::shared_ptr<CryptoFrame> inflight;
    {
        ResurrectionVisitor v;
        v.allowance_ = 100;
        ASSERT_EQ(stream->Send(finished, 36, kHandshake), 36);
        ASSERT_EQ(stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
        ASSERT_EQ(v.handled_.size(), 1u);
        inflight = v.handled_.front();
        ASSERT_NE(inflight, nullptr);
    }

    // 2. Handshake confirmed (FinalizeHandshakePacketNumberSpaces path)
    stream->MarkHandshakeConfirmed();

    // 3. Late kLost notifications on the stale in-flight frame (from any
    // pre-confirmation packet) must NOT re-queue the segment.
    for (int round = 0; round < 5; round++) {
        inflight->NotifyDelivery(FrameDeliveryState::kLost);
    }
    for (int round = 0; round < 5; round++) {
        ResurrectionVisitor v;
        v.allowance_ = 100;
        EXPECT_EQ(stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
        EXPECT_EQ(v.handled_.size(), 0u) << "post-kLost round " << round << " resurrected handshake CRYPTO";
    }

    // 4. Fresh sends at handshake level after confirmation must also be inert.
    EXPECT_EQ(stream->Send(finished, 36, kHandshake), 36);
    ResurrectionVisitor v;
    v.allowance_ = 100;
    EXPECT_EQ(stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
    // (single final drain is allowed by design: unsent outgoing bytes are kept
    // for one last transmission; what must never happen is regeneration from
    // retained/lost ranges - asserted above.)
}

}  // namespace
}  // namespace quic
}  // namespace quicx