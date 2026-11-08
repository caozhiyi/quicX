#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "quic/frame/stream_frame.h"
#include "quic/stream/send_stream.h"

// P0-C10 / D2: frame-level STREAM retransmission. SendControl notifies the
// STREAM frames of a lost / probed packet through IFrame::NotifyDelivery;
// SendStream must re-queue exactly the un-ACKed byte ranges (CryptoStream's
// "uncovered gaps" algorithm, zero-copy from the lost frame's own span),
// restore the FIN bit, bypass the peer flow-control gate, and never charge
// connection-level flow control a second time.

namespace quicx {
namespace quic {
namespace {

class RetxVisitor: public IFrameVisitor {
public:
    bool HandleFrame(std::shared_ptr<IFrame> frame) override {
        auto sf = std::dynamic_pointer_cast<StreamFrame>(frame);
        if (!accept_) {
            return false;
        }
        if (sf) {
            frames_.push_back(sf);
            // Model the packet filling up: payload + the 24 B header reserve
            // SendStream budgets per STREAM frame.
            const uint32_t used = sf->GetLength() + 24;
            packet_left_ = packet_left_ > used ? packet_left_ - used : 0;
        }
        return true;
    }
    std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
    uint8_t GetEncryptionLevel() override { return kApplication; }
    void SetStreamDataSizeLimit(uint32_t size) override { allowance_ = size; }
    uint32_t GetLeftStreamDataSize() override { return allowance_; }
    void AddStreamDataSize(uint32_t size) override {
        charged_ += size;
        allowance_ -= std::min(allowance_, size);
    }
    uint64_t GetStreamDataSize() override { return 0; }
    uint32_t GetPacketLeftSize() override { return packet_left_; }
    FrameEncodeError GetLastError() const override { return last_error_; }

    std::vector<std::shared_ptr<StreamFrame>> frames_;
    bool accept_{true};
    uint32_t allowance_{100000};
    uint32_t packet_left_{1400};
    uint64_t charged_{0};
    FrameEncodeError last_error_{FrameEncodeError::kNone};
};

class SendStreamRetransmitTest: public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = common::MakeEventLoop();
        ASSERT_TRUE(loop_->Init());
    }

    std::shared_ptr<SendStream> MakeStream(uint64_t peer_limit = 1000000) {
        return std::make_shared<SendStream>(
            loop_, peer_limit, 4, [](std::shared_ptr<IStream>) {}, [](uint64_t) {},
            [](uint64_t, uint16_t, const std::string&) {});
    }

    static std::vector<uint8_t> Pattern(size_t n) {
        std::vector<uint8_t> v(n);
        for (size_t i = 0; i < n; ++i) {
            v[i] = static_cast<uint8_t>(i * 7 + 3);
        }
        return v;
    }

    static std::vector<uint8_t> BytesOf(const std::shared_ptr<StreamFrame>& f) {
        auto data = f->GetData();
        if (!data.Valid()) {
            return {};
        }
        return std::vector<uint8_t>(data.GetStart(), data.GetStart() + data.GetLength());
    }

    std::shared_ptr<common::IEventLoop> loop_;
};

TEST_F(SendStreamRetransmitTest, SentFrameCarriesDeliveryHandler) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor v;
    stream->TrySendData(&v);
    ASSERT_EQ(v.frames_.size(), 1u);
    EXPECT_TRUE(v.frames_[0]->HasDeliveryHandler());
}

TEST_F(SendStreamRetransmitTest, LostFrameIsReemittedWithSameBytesAndNotChargedTwice) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);
    EXPECT_EQ(first.charged_, 100u);

    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_TRUE(stream->HasPendingRetransmit());

    RetxVisitor second;
    stream->TrySendData(&second);
    ASSERT_EQ(second.frames_.size(), 1u);
    EXPECT_EQ(second.frames_[0]->GetOffset(), 0u);
    EXPECT_EQ(BytesOf(second.frames_[0]), bytes);
    EXPECT_EQ(second.charged_, 0u) << "retransmitted bytes must not be charged to connection flow control again";
    EXPECT_FALSE(stream->HasPendingRetransmit());
}

TEST_F(SendStreamRetransmitTest, ProbeRequeuesAndRepeatedProbeIsIdempotent) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);

    // kProbe is never latched and must not double-queue.
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kProbe);
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kProbe);
    EXPECT_EQ(stream->GetPendingRetransmitSegments(), 1u);

    // kLost after a probe of the same frame: the range is already queued.
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(stream->GetPendingRetransmitSegments(), 1u);
}

TEST_F(SendStreamRetransmitTest, LostRangeAlreadyAckedIsNotRequeued) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor v;
    stream->TrySendData(&v);
    ASSERT_EQ(v.frames_.size(), 1u);

    stream->OnDataAcked(0, 100, false);  // late ACK wins the race
    v.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_FALSE(stream->HasPendingRetransmit());
}

TEST_F(SendStreamRetransmitTest, OnlyUnackedGapsOfALostFrameAreRequeued) {
    auto stream = MakeStream();
    auto bytes = Pattern(300);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 300), 300);

    RetxVisitor v;
    stream->TrySendData(&v);
    ASSERT_EQ(v.frames_.size(), 1u);
    ASSERT_EQ(v.frames_[0]->GetLength(), 300u);

    stream->OnDataAcked(100, 100, false);  // middle [100,200) ACKed via another path
    v.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(stream->GetPendingRetransmitSegments(), 2u);

    RetxVisitor out;
    stream->TrySendData(&out);
    ASSERT_EQ(out.frames_.size(), 2u);
    EXPECT_EQ(out.frames_[0]->GetOffset(), 0u);
    EXPECT_EQ(out.frames_[0]->GetLength(), 100u);
    EXPECT_EQ(out.frames_[1]->GetOffset(), 200u);
    EXPECT_EQ(out.frames_[1]->GetLength(), 100u);
    EXPECT_EQ(BytesOf(out.frames_[1]), std::vector<uint8_t>(bytes.begin() + 200, bytes.end()));
}

TEST_F(SendStreamRetransmitTest, LostFinIsRestoredOnRetransmissionInDataSentState) {
    auto stream = MakeStream();
    auto bytes = Pattern(50);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 50), 50);
    stream->Close();

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);
    ASSERT_TRUE(first.frames_[0]->IsFin());
    // FIN is out: the stream is in Data Sent, where new STREAM frames are
    // forbidden — but retransmission must still work.
    ASSERT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataSent);

    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    RetxVisitor second;
    stream->TrySendData(&second);
    ASSERT_EQ(second.frames_.size(), 1u);
    EXPECT_EQ(second.frames_[0]->GetOffset(), 0u);
    EXPECT_EQ(BytesOf(second.frames_[0]), bytes);
    EXPECT_TRUE(second.frames_[0]->IsFin()) << "the retransmitted tail must restore the FIN bit";
}

TEST_F(SendStreamRetransmitTest, LostFinOnlyFrameIsRetransmitted) {
    auto stream = MakeStream();
    auto bytes = Pattern(50);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 50), 50);

    RetxVisitor data_pkt;
    stream->TrySendData(&data_pkt);
    ASSERT_EQ(data_pkt.frames_.size(), 1u);
    stream->OnDataAcked(0, 50, false);  // data fully acknowledged

    stream->Close();
    RetxVisitor fin_pkt;
    stream->TrySendData(&fin_pkt);
    ASSERT_EQ(fin_pkt.frames_.size(), 1u);
    ASSERT_TRUE(fin_pkt.frames_[0]->IsFin());
    ASSERT_EQ(fin_pkt.frames_[0]->GetLength(), 0u);

    // The FIN-only packet is lost. Because the FIN has not been ACKed the
    // stream must NOT be Data Recvd — and the FIN must come back.
    EXPECT_NE(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    fin_pkt.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);

    RetxVisitor retx;
    stream->TrySendData(&retx);
    ASSERT_EQ(retx.frames_.size(), 1u);
    EXPECT_EQ(retx.frames_[0]->GetOffset(), 50u);
    EXPECT_EQ(retx.frames_[0]->GetLength(), 0u);
    EXPECT_TRUE(retx.frames_[0]->IsFin());

    // The retransmitted FIN is finally acknowledged: now terminal.
    stream->OnDataAcked(50, 0, true);
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
}

TEST_F(SendStreamRetransmitTest, AckedFinSuppressesFinRetransmission) {
    auto stream = MakeStream();
    auto bytes = Pattern(50);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 50), 50);
    stream->Close();

    RetxVisitor v;
    stream->TrySendData(&v);
    ASSERT_EQ(v.frames_.size(), 1u);
    ASSERT_TRUE(v.frames_[0]->IsFin());

    stream->OnDataAcked(0, 50, true);  // everything incl. FIN acked
    v.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_FALSE(stream->HasPendingRetransmit());
}

TEST_F(SendStreamRetransmitTest, RetransmissionBypassesPeerFlowControlGate) {
    // The peer limit equals the bytes sent: new data is flow-control blocked,
    // but re-sending already-admitted bytes must not be.
    auto stream = MakeStream(100);
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);

    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    RetxVisitor second;
    stream->TrySendData(&second);
    ASSERT_EQ(second.frames_.size(), 1u);
    EXPECT_EQ(BytesOf(second.frames_[0]), bytes);
}

TEST_F(SendStreamRetransmitTest, PacketBudgetSplitsRetransmissionAcrossPackets) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);

    RetxVisitor small1;
    small1.packet_left_ = 24 + 40;  // header reserve + 40 payload bytes
    EXPECT_EQ(stream->TrySendData(&small1), IStream::TrySendResult::kBreak);
    ASSERT_EQ(small1.frames_.size(), 1u);
    EXPECT_EQ(small1.frames_[0]->GetOffset(), 0u);
    EXPECT_EQ(small1.frames_[0]->GetLength(), 40u);
    EXPECT_TRUE(stream->HasPendingRetransmit());

    RetxVisitor rest;
    stream->TrySendData(&rest);
    ASSERT_EQ(rest.frames_.size(), 1u);
    EXPECT_EQ(rest.frames_[0]->GetOffset(), 40u);
    EXPECT_EQ(rest.frames_[0]->GetLength(), 60u);
    EXPECT_EQ(BytesOf(rest.frames_[0]), std::vector<uint8_t>(bytes.begin() + 40, bytes.end()));
    EXPECT_FALSE(stream->HasPendingRetransmit());
}

TEST_F(SendStreamRetransmitTest, AckArrivingWhileQueuedTrimsRetransmission) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);

    stream->OnDataAcked(0, 60, false);  // a late ACK covers the front before we re-send

    RetxVisitor out;
    stream->TrySendData(&out);
    ASSERT_EQ(out.frames_.size(), 1u);
    EXPECT_EQ(out.frames_[0]->GetOffset(), 60u);
    EXPECT_EQ(out.frames_[0]->GetLength(), 40u);
    EXPECT_EQ(BytesOf(out.frames_[0]), std::vector<uint8_t>(bytes.begin() + 60, bytes.end()));
}

TEST_F(SendStreamRetransmitTest, ResetStreamAbandonsRetransmission) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    ASSERT_TRUE(stream->HasPendingRetransmit());

    stream->Reset(7);
    EXPECT_FALSE(stream->HasPendingRetransmit());

    // A loss reported after the reset must not resurrect the data.
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kProbe);
    EXPECT_FALSE(stream->HasPendingRetransmit());
}

TEST_F(SendStreamRetransmitTest, RetransmittedFrameCanBeLostAgain) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor gen0;
    stream->TrySendData(&gen0);
    gen0.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);

    RetxVisitor gen1;
    stream->TrySendData(&gen1);
    ASSERT_EQ(gen1.frames_.size(), 1u);
    ASSERT_TRUE(gen1.frames_[0]->HasDeliveryHandler());

    // The retransmission is a fresh frame instance with its own loss budget
    // (RFC 9000 §13.3: one retransmission per loss generation).
    gen1.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);
    RetxVisitor gen2;
    stream->TrySendData(&gen2);
    ASSERT_EQ(gen2.frames_.size(), 1u);
    EXPECT_EQ(BytesOf(gen2.frames_[0]), bytes);
}

TEST_F(SendStreamRetransmitTest, StreamNotCompleteUntilFinAcknowledged) {
    auto stream = MakeStream();
    auto bytes = Pattern(10);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 10), 10);
    stream->Close();

    RetxVisitor v;
    stream->TrySendData(&v);
    ASSERT_EQ(v.frames_.size(), 1u);

    stream->OnDataAcked(0, 10, false);  // bytes acked, FIN not (yet)
    EXPECT_NE(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    stream->OnDataAcked(10, 0, true);
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
}


// ---------------------------------------------------------------------------
// Review item 4: an ACK that lands in the MIDDLE of an already-queued segment
// must not be re-sent. Queue-time gap computation cannot see it; the emission
// has to stop at the acknowledged range, and the tail beyond it is trimmed.
// ---------------------------------------------------------------------------

TEST_F(SendStreamRetransmitTest, MidSegmentAckAfterQueueingIsNotResent) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);
    first.frames_[0]->NotifyDelivery(FrameDeliveryState::kLost);  // [0,100) queued whole

    stream->OnDataAcked(30, 30, false);  // out-of-order ACK for [30,60), middle of the segment

    RetxVisitor out;
    stream->TrySendData(&out);
    // Exactly [0,30) and [60,100): 70 bytes, none of the acknowledged 30.
    uint64_t total = 0;
    std::vector<std::pair<uint64_t, uint64_t>> spans;
    for (const auto& fr : out.frames_) {
        spans.emplace_back(fr->GetOffset(), fr->GetOffset() + fr->GetLength());
        total += fr->GetLength();
    }
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0], (std::pair<uint64_t, uint64_t>(0, 30)));
    EXPECT_EQ(spans[1], (std::pair<uint64_t, uint64_t>(60, 100)));
    EXPECT_EQ(total, 70u) << "no redundant byte may be re-sent";
    EXPECT_EQ(BytesOf(out.frames_[0]), std::vector<uint8_t>(bytes.begin(), bytes.begin() + 30));
    EXPECT_EQ(BytesOf(out.frames_[1]), std::vector<uint8_t>(bytes.begin() + 60, bytes.end()));
    EXPECT_FALSE(stream->HasPendingRetransmit());
}

TEST_F(SendStreamRetransmitTest, FinRidesOnlyTheTrueTailWhenAMiddleRangeIsAcked) {
    auto stream = MakeStream();
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);
    stream->Close();

    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_FALSE(first.frames_.empty());
    for (auto& fr : first.frames_) fr->NotifyDelivery(FrameDeliveryState::kLost);

    stream->OnDataAcked(30, 30, false);

    RetxVisitor out;
    stream->TrySendData(&out);
    ASSERT_EQ(out.frames_.size(), 2u);
    EXPECT_FALSE(out.frames_[0]->IsFin()) << "the piece cut short by the acked range must not carry FIN";
    EXPECT_TRUE(out.frames_[1]->IsFin());
    EXPECT_EQ(out.frames_[1]->GetOffset() + out.frames_[1]->GetLength(), 100u);
}

// ---------------------------------------------------------------------------
// Review item 2: stream-level control frames leave BEFORE the flow-control
// gates, and a full packet is kBreak (stay active), never kFailed (dropped
// from the active list with the frame stranded in frames_list_).
// ---------------------------------------------------------------------------

TEST_F(SendStreamRetransmitTest, ResetStreamIsSentEvenWhenFlowControlBlocked) {
    // Peer limit == bytes sent: the stream is flow-control blocked.
    auto stream = MakeStream(100);
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);
    RetxVisitor first;
    stream->TrySendData(&first);
    ASSERT_EQ(first.frames_.size(), 1u);

    stream->Reset(7);

    class FrameCatcher: public RetxVisitor {
    public:
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kResetStream) {
                ++resets;
                return true;
            }
            return RetxVisitor::HandleFrame(frame);
        }
        int resets{0};
    } out;
    stream->TrySendData(&out);
    EXPECT_EQ(out.resets, 1) << "RESET_STREAM must not sit behind the peer-limit gate";
}

TEST_F(SendStreamRetransmitTest, LostResetStreamIsResentWhileFlowControlBlocked) {
    auto stream = MakeStream(100);
    auto bytes = Pattern(100);
    ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), 100), 100);
    RetxVisitor first;
    stream->TrySendData(&first);
    stream->Reset(9);

    class FrameCatcher: public RetxVisitor {
    public:
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kResetStream) {
                reset = frame;
                return true;
            }
            return RetxVisitor::HandleFrame(frame);
        }
        std::shared_ptr<IFrame> reset;
    } a, b;
    stream->TrySendData(&a);
    ASSERT_NE(a.reset, nullptr);

    a.reset->NotifyDelivery(FrameDeliveryState::kLost);  // re-queued by TrackControlFrame
    stream->TrySendData(&b);
    EXPECT_NE(b.reset, nullptr) << "the re-queued RESET_STREAM must also get past the flow-control gate";
}

TEST_F(SendStreamRetransmitTest, ControlFrameThatDoesNotFitIsBreakNotFailed) {
    auto stream = MakeStream();
    stream->Reset(3);

    class FullPacket: public RetxVisitor {
    public:
        bool HandleFrame(std::shared_ptr<IFrame>) override {
            last_error_ = FrameEncodeError::kInsufficientSpace;
            return false;
        }
    } full;
    EXPECT_EQ(stream->TrySendData(&full), IStream::TrySendResult::kBreak)
        << "packet full is retried next packet; kFailed would drop the stream from the active list";

    // The frame was not lost: the next packet still carries it.
    class FrameCatcher: public RetxVisitor {
    public:
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kResetStream) ++resets;
            return true;
        }
        int resets{0};
    } next;
    stream->TrySendData(&next);
    EXPECT_EQ(next.resets, 1);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
