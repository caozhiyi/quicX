#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/new_connection_id_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(NewConnectionIdFrameTest, codec) {
    NewConnectionIDFrame frame1;
    NewConnectionIDFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(256));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(256));

    frame1.SetRetirePriorTo(10086);
    frame1.SetSequenceNumber(2352632);

    uint8_t toekn[128] = {0};
    for (size_t i = 0; i < 128; i++) {
        toekn[i] = i;
    }

    frame1.SetStatelessResetToken((uint8_t*)toekn);

    uint8_t cid_bytes[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t len = sizeof(cid_bytes);
    frame1.SetConnectionID(cid_bytes, len);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();
    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetRetirePriorTo(), frame2.GetRetirePriorTo());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());

    ConnectionID cid;
    frame2.GetConnectionID(cid);
    EXPECT_EQ(std::string((char*)cid.GetID(), cid.GetLength()), std::string((char*)cid_bytes, len));
    EXPECT_EQ(std::string((char*)frame1.GetStatelessResetToken(), kStatelessResetTokenLength),
        std::string((char*)frame2.GetStatelessResetToken(), kStatelessResetTokenLength));
}

// RFC 9000 §13.3: a lost frame is retransmitted once per loss generation.
// IFrame::NotifyDelivery latches kLost per frame instance, so repeated
// notifications for the same packet (DetectLostPackets followed by PTO
// re-queue cycles) produce exactly ONE re-emission, not one per notification
// — the repeatable path is what turned one lost NCID frame into 14,081
// re-emissions in 55 seconds (interop qlog 2026-10-21, quicx_aioquic).
// A fresh re-emitted instance is a NEW frame object and must get its own
// notification budget.
TEST(NewConnectionIdFrameTest, kLostNotifiedAtMostOncePerInstance) {
    int lost_count = 0;
    int acked_count = 0;

    NewConnectionIDFrame original;
    original.SetDeliveryHandler([&lost_count, &acked_count](FrameDeliveryState state) {
        if (state == FrameDeliveryState::kLost) {
            ++lost_count;
        } else if (state == FrameDeliveryState::kAcked) {
            ++acked_count;
        }
    });

    // First loss notification: delivered.
    original.NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(lost_count, 1);

    // Repeat notifications for the same instance (PTO re-queue fires the same
    // packet's frames every cycle): suppressed.
    original.NotifyDelivery(FrameDeliveryState::kLost);
    original.NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(lost_count, 1);

    // kAcked is not latched — every ack notification still reaches the handler.
    original.NotifyDelivery(FrameDeliveryState::kAcked);
    original.NotifyDelivery(FrameDeliveryState::kAcked);
    EXPECT_EQ(acked_count, 2);

    // The next generation (a re-emitted copy) is a fresh instance: its kLost
    // budget is independent, so the §13.3 chain still retransmits on ITS loss.
    NewConnectionIDFrame next_gen;
    next_gen.SetDeliveryHandler([&lost_count, &acked_count](FrameDeliveryState state) {
        if (state == FrameDeliveryState::kLost) {
            ++lost_count;
        } else if (state == FrameDeliveryState::kAcked) {
            ++acked_count;
        }
    });
    next_gen.NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(lost_count, 2);
    next_gen.NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(lost_count, 2);  // and latched for this instance too
}

}  // namespace
}  // namespace quic
}  // namespace quicx