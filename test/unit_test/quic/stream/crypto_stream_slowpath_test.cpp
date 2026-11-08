#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "common/buffer/single_block_buffer.h"

#include "quic/connection/controller/retransmit_scheduler.h"
#include "quic/frame/crypto_frame.h"
#include "quic/stream/crypto_stream.h"

#include "test/unit_test/common/timer/test_timer_scheduler.h"

// P0-C10 / X3: CryptoStream as a slow-path retransmit owner. Slot == level.
//  - armed when bytes of a handshake level first go on the wire,
//  - base-PTO expiry requeues (kProbe) only what is neither ACKed nor queued,
//  - ranges ACKed BEYOND the contiguous prefix are not resent (server §8.1
//    amplification budget is precious),
//  - disarmed when everything is ACKed / the level's keys are gone.

namespace quicx {
namespace quic {
namespace {

constexpr uint32_t kBasePto = 100;

class SlowPathVisitor: public IFrameVisitor {
public:
    bool HandleFrame(std::shared_ptr<IFrame> frame) override {
        auto cf = std::dynamic_pointer_cast<CryptoFrame>(frame);
        if (cf) {
            frames_.push_back(cf);
        }
        return true;
    }
    std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
    uint8_t GetEncryptionLevel() override { return kHandshake; }
    void SetStreamDataSizeLimit(uint32_t size) override { allowance_ = size; }
    uint32_t GetLeftStreamDataSize() override { return allowance_; }
    uint64_t GetStreamDataSize() override { return 0; }
    uint32_t GetPacketLeftSize() override { return packet_left_; }

    std::vector<std::shared_ptr<CryptoFrame>> frames_;
    uint32_t allowance_{100000};
    uint32_t packet_left_{1400};
};

class CryptoSlowPathTest: public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = common::MakeEventLoop();
        ASSERT_TRUE(loop_->Init());
        timers_ = std::make_shared<common::TestTimerScheduler>();
        life_ = std::make_shared<int>(0);
        sched_ = std::make_shared<RetransmitScheduler>(
            timers_, [this] { return timers_->Now(); }, [] { return kBasePto; });
        sched_->SetLifeToken(life_);
        stream_ = std::make_shared<CryptoStream>(loop_, nullptr, nullptr, nullptr);
        stream_->SetRetransmitScheduler(sched_);
    }

    // Queue `n` bytes at the Handshake level and emit them in packets of
    // `payload_per_packet` bytes; returns every frame emitted.
    std::vector<std::shared_ptr<CryptoFrame>> SendAndEmit(size_t n, uint32_t payload_per_packet = 1380) {
        std::vector<uint8_t> bytes(n);
        for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(i);
        EXPECT_EQ(stream_->Send(bytes.data(), static_cast<uint32_t>(n), kHandshake), static_cast<int32_t>(n));
        std::vector<std::shared_ptr<CryptoFrame>> out;
        while (stream_->HasPendingDataAt(kHandshake)) {
            SlowPathVisitor v;
            v.packet_left_ = 20 + payload_per_packet;  // 20 B CRYPTO header reserve
            v.allowance_ = payload_per_packet;
            stream_->TrySendData(&v, kHandshake);
            if (v.frames_.empty()) break;
            out.insert(out.end(), v.frames_.begin(), v.frames_.end());
        }
        return out;
    }

    std::vector<std::shared_ptr<CryptoFrame>> EmitPending() {
        std::vector<std::shared_ptr<CryptoFrame>> out;
        while (stream_->HasPendingDataAt(kHandshake)) {
            SlowPathVisitor v;
            stream_->TrySendData(&v, kHandshake);
            if (v.frames_.empty()) break;
            out.insert(out.end(), v.frames_.begin(), v.frames_.end());
        }
        return out;
    }

    std::shared_ptr<common::IEventLoop> loop_;
    std::shared_ptr<common::TestTimerScheduler> timers_;
    std::shared_ptr<int> life_;
    std::shared_ptr<RetransmitScheduler> sched_;
    std::shared_ptr<CryptoStream> stream_;
};

TEST_F(CryptoSlowPathTest, EmittingHandshakeBytesArmsTheSlot) {
    EXPECT_FALSE(sched_->IsArmed(stream_.get(), kHandshake));
    auto frames = SendAndEmit(100);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(sched_->IsArmed(stream_.get(), kHandshake));
    EXPECT_FALSE(sched_->IsArmed(stream_.get(), kInitial)) << "slot is per level";
}

TEST_F(CryptoSlowPathTest, ApplicationLevelBytesNeverArmTheSlot) {
    // 1-RTT CRYPTO (NewSessionTicket) is ACK/PTO-driven, like the old
    // per-packet timer which only acted on Initial/Handshake.
    std::vector<uint8_t> bytes(50, 1);
    ASSERT_EQ(stream_->Send(bytes.data(), 50, kApplication), 50);
    SlowPathVisitor v;
    stream_->TrySendData(&v, kApplication);
    ASSERT_EQ(v.frames_.size(), 1u);
    EXPECT_EQ(sched_->Size(), 0u);
}

TEST_F(CryptoSlowPathTest, BasePtoExpiryRequeuesTheOutstandingWindowAsProbe) {
    auto first = SendAndEmit(100);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_FALSE(stream_->HasPendingDataAt(kHandshake));

    timers_->Advance(kBasePto + 1);

    ASSERT_TRUE(stream_->HasPendingDataAt(kHandshake)) << "the slow path must put unacked bytes back on the wire";
    auto again = EmitPending();
    ASSERT_EQ(again.size(), 1u);
    EXPECT_EQ(again[0]->GetOffset(), 0u);
    EXPECT_EQ(again[0]->GetLength(), 100u);
    // Cadence continues: still armed for the next base PTO.
    EXPECT_TRUE(sched_->IsArmed(stream_.get(), kHandshake));
}

TEST_F(CryptoSlowPathTest, CadenceKeepsRequeueingUntilAcked) {
    SendAndEmit(100);
    for (int round = 0; round < 4; ++round) {
        timers_->Advance(kBasePto + 1);
        auto again = EmitPending();
        ASSERT_EQ(again.size(), 1u) << "round " << round << ": one retransmission per base PTO";
        EXPECT_EQ(again[0]->GetOffset(), 0u);
    }
}

TEST_F(CryptoSlowPathTest, AckedOutOfOrderRangeIsNotResent) {
    // 100 bytes in 40/40/20 packets; only the MIDDLE packet is acknowledged.
    auto frames = SendAndEmit(100, 40);
    ASSERT_EQ(frames.size(), 3u);
    ASSERT_EQ(frames[1]->GetOffset(), 40u);
    frames[1]->NotifyDelivery(FrameDeliveryState::kAcked);

    timers_->Advance(kBasePto + 1);
    auto again = EmitPending();

    // Exactly [0,40) and [80,100) — the delivered middle must not be resent.
    uint64_t total = 0;
    for (const auto& f : again) {
        total += f->GetLength();
        const bool in_middle = f->GetOffset() < 80 && f->GetOffset() + f->GetLength() > 40;
        EXPECT_FALSE(in_middle) << "resent an already-ACKed range at offset " << f->GetOffset();
    }
    EXPECT_EQ(total, 60u);
}

TEST_F(CryptoSlowPathTest, FullAckDisarmsTheSlot) {
    auto frames = SendAndEmit(100, 40);
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_TRUE(sched_->IsArmed(stream_.get(), kHandshake));

    frames[0]->NotifyDelivery(FrameDeliveryState::kAcked);
    frames[2]->NotifyDelivery(FrameDeliveryState::kAcked);
    EXPECT_TRUE(sched_->IsArmed(stream_.get(), kHandshake)) << "gap in the middle: still outstanding";

    frames[1]->NotifyDelivery(FrameDeliveryState::kAcked);  // closes the gap
    EXPECT_FALSE(sched_->IsArmed(stream_.get(), kHandshake));

    timers_->Advance(kBasePto * 3);
    EXPECT_FALSE(stream_->HasPendingDataAt(kHandshake));
}

TEST_F(CryptoSlowPathTest, ProbeAndLostDoNotDoubleQueue) {
    auto frames = SendAndEmit(100);
    ASSERT_EQ(frames.size(), 1u);
    frames[0]->NotifyDelivery(FrameDeliveryState::kProbe);
    frames[0]->NotifyDelivery(FrameDeliveryState::kLost);  // same range: already queued
    timers_->Advance(kBasePto + 1);                         // slow path also asks for it

    auto again = EmitPending();
    uint64_t total = 0;
    for (const auto& f : again) total += f->GetLength();
    EXPECT_EQ(total, 100u) << "the range is queued once however many paths ask for it";
}

TEST_F(CryptoSlowPathTest, HandshakeConfirmedDisarmsAndStopsTheCadence) {
    SendAndEmit(100);
    ASSERT_TRUE(sched_->IsArmed(stream_.get(), kHandshake));

    stream_->MarkHandshakeConfirmed();
    EXPECT_FALSE(sched_->IsArmed(stream_.get(), kHandshake));

    timers_->Advance(kBasePto * 3);
    EXPECT_FALSE(stream_->HasPendingDataAt(kHandshake)) << "no probe storm after confirmation (crosstraffic lesson)";
}

TEST_F(CryptoSlowPathTest, InitialConsumedByPeerDisarmsInitialOnly) {
    std::vector<uint8_t> init_bytes(60, 2), hs_bytes(60, 3);
    ASSERT_EQ(stream_->Send(init_bytes.data(), 60, kInitial), 60);
    ASSERT_EQ(stream_->Send(hs_bytes.data(), 60, kHandshake), 60);
    SlowPathVisitor vi, vh;
    stream_->TrySendData(&vi, kInitial);
    stream_->TrySendData(&vh, kHandshake);
    ASSERT_TRUE(sched_->IsArmed(stream_.get(), kInitial));
    ASSERT_TRUE(sched_->IsArmed(stream_.get(), kHandshake));

    stream_->MarkInitialConsumedByPeer();
    EXPECT_FALSE(sched_->IsArmed(stream_.get(), kInitial));
    EXPECT_TRUE(sched_->IsArmed(stream_.get(), kHandshake));
}

TEST_F(CryptoSlowPathTest, DestroyingTheStreamUnregistersItFromTheScheduler) {
    SendAndEmit(100);
    ASSERT_EQ(sched_->Size(), 1u);
    stream_.reset();
    EXPECT_EQ(sched_->Size(), 0u) << "the scheduler must not keep a dangling owner pointer";
    timers_->Advance(kBasePto * 3);  // would crash / UAF if it still fired
}

TEST_F(CryptoSlowPathTest, SchedulerOutlivingOrPredeceasingTheStreamIsSafe) {
    SendAndEmit(100);
    sched_.reset();  // SendControl gone first
    stream_.reset();  // stream destructor must tolerate the dead scheduler
    SUCCEED();
}


// ---------------------------------------------------------------------------
// Slow-path deadline follows the OLDEST UNACKED bytes, and a probe only
// re-sends bytes whose last transmission is at least one base PTO old.
// (Review item: Arm() keeps the first send's deadline, so after a partial ACK
// the probe also re-sent younger bytes -- redundant, and under the server's 8.1
// budget it starves bytes that were genuinely lost.)
// ---------------------------------------------------------------------------

TEST_F(CryptoSlowPathTest, ProbeSkipsBytesSentLessThanOnePtoAgo) {
    // t=0: 100 bytes in 40/40/20 packets.
    const uint64_t t0 = timers_->Now();
    auto flight = SendAndEmit(100, 40);
    ASSERT_EQ(flight.size(), 3u);

    // t=60: the first packet is acknowledged (prefix -> 40) and 50 NEW bytes go out.
    timers_->Advance(60);
    flight[0]->NotifyDelivery(FrameDeliveryState::kAcked);
    auto fresh = SendAndEmit(50, 50);
    ASSERT_EQ(fresh.size(), 1u);
    ASSERT_EQ(fresh[0]->GetOffset(), 100u);

    // t=105: the deadline set by the t=0 flight (100) has passed. The old bytes
    // [40,100) are 105ms old, the new [100,150) only 45ms old (< base PTO 100).
    timers_->Advance(45);
    auto probe = EmitPending();
    ASSERT_FALSE(probe.empty()) << "the old, unacknowledged bytes must be probed";
    uint64_t lowest = UINT64_MAX, highest = 0;
    for (const auto& f : probe) {
        lowest = std::min<uint64_t>(lowest, f->GetOffset());
        highest = std::max<uint64_t>(highest, f->GetOffset() + f->GetLength());
    }
    EXPECT_EQ(lowest, 40u);
    EXPECT_LE(highest, 100u) << "bytes [100,150) were sent 45ms ago -- they must NOT be re-sent yet";

    // The young bytes still get their own deadline: 60 + 100 = 160, sooner than
    // the scheduler's generic post-fire restart (105 + 100).
    EXPECT_EQ(sched_->NextDeadlineMs(), t0 + 160);
    timers_->Advance(56);  // t=161
    auto later = EmitPending();
    ASSERT_FALSE(later.empty());
    EXPECT_EQ(later.back()->GetOffset() + later.back()->GetLength(), 150u)
        << "once a full PTO has passed, the young bytes are probed too";
}

TEST_F(CryptoSlowPathTest, PartialAckMovesTheDeadlineToTheOldestUnackedBytes) {
    // t=0: first packet; t=60: second packet.
    const uint64_t t0 = timers_->Now();
    auto a = SendAndEmit(50, 50);
    ASSERT_EQ(a.size(), 1u);
    timers_->Advance(60);
    auto b = SendAndEmit(50, 50);
    ASSERT_EQ(b.size(), 1u);
    ASSERT_EQ(sched_->NextDeadlineMs(), t0 + 100) << "armed by the t=0 send";

    // t=70: the first packet is acknowledged. The only outstanding bytes are
    // the t=60 ones, so the deadline must move to 60 + 100 -- not stay at 100.
    timers_->Advance(10);
    a[0]->NotifyDelivery(FrameDeliveryState::kAcked);
    EXPECT_EQ(sched_->NextDeadlineMs(), t0 + 160);

    // At t=105 nothing may be re-sent: the outstanding bytes are 45ms old.
    timers_->Advance(35);
    EXPECT_FALSE(stream_->HasPendingDataAt(kHandshake)) << "a redundant probe for 45ms-old bytes";

    // At t=161 they are a full PTO old: now they are probed.
    timers_->Advance(56);
    EXPECT_TRUE(stream_->HasPendingDataAt(kHandshake));
}

TEST_F(CryptoSlowPathTest, ResentBytesCarryTheirNewTimestamp) {
    // A retransmission restarts the clock for exactly those bytes.
    SendAndEmit(100);
    timers_->Advance(101);  // t=101: probe fires, bytes requeued
    auto resent = EmitPending();
    ASSERT_EQ(resent.size(), 1u);

    // t=150: only 49ms since the re-send. No probe yet.
    timers_->Advance(49);
    EXPECT_FALSE(stream_->HasPendingDataAt(kHandshake));
    // t=202: a full PTO since the re-send.
    timers_->Advance(52);
    EXPECT_TRUE(stream_->HasPendingDataAt(kHandshake));
}

}  // namespace
}  // namespace quic
}  // namespace quicx
