#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "common/buffer/single_block_buffer.h"

#include "quic/frame/crypto_frame.h"
#include "quic/stream/crypto_stream.h"

// Frame-level delivery wiring for the CRYPTO stream (RFC 9000 §7.5
// reliability via offset): SendControl's FireFrameDelivery notifies every
// tracked frame with kAcked/kLost; CryptoStream's handler advances the acked
// watermark on the former and re-queues the lost range as a fresh outgoing
// segment on the latter, so the normal send path can size the re-emission to
// whatever budget remains (the build-time §8.1 amp constraint). These tests
// pin that contract at the stream level, without a connection.

namespace quicx {
namespace quic {
namespace {

// Visitor mock: hands back the frames it accepted and lets the test steer
// both size caps (packet left space and stream-data allowance) that
// CryptoStream::TrySendData consults.
class DeliveryTestVisitor: public IFrameVisitor {
public:
    bool HandleFrame(std::shared_ptr<IFrame> frame) override {
        handled_.push_back(std::dynamic_pointer_cast<CryptoFrame>(frame));
        return accept_;
    }
    std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
    uint8_t GetEncryptionLevel() override { return level_; }
    void SetStreamDataSizeLimit(uint32_t size) override { stream_allowance_ = size; }
    uint32_t GetLeftStreamDataSize() override { return stream_allowance_; }
    void AddStreamDataSize(uint32_t size) override { stream_allowance_ -= std::min(stream_allowance_, size); }
    uint64_t GetStreamDataSize() override { return 0; }
    uint32_t GetPacketLeftSize() override { return packet_left_; }

    std::vector<std::shared_ptr<CryptoFrame>> handled_;
    bool accept_{true};
    uint8_t level_{kHandshake};
    uint32_t packet_left_{1400};
    uint32_t stream_allowance_{0};
};

struct Fixture {
    std::shared_ptr<common::IEventLoop> loop;
    std::shared_ptr<CryptoStream> stream;

    Fixture() {
        loop = common::MakeEventLoop();
        if (!loop->Init()) {
            ADD_FAILURE() << "event loop init failed";
        }
        stream = std::make_shared<CryptoStream>(loop, nullptr, nullptr, nullptr);
    }

    void Send(uint8_t level, const std::vector<uint8_t>& bytes) {
        ASSERT_EQ(stream->Send(const_cast<uint8_t*>(bytes.data()), (uint32_t)bytes.size(), level),
            (int32_t)bytes.size());
    }

    static std::vector<uint8_t> Bytes(size_t n, uint8_t seed) {
        std::vector<uint8_t> v(n);
        for (size_t i = 0; i < n; ++i) {
            v[i] = (uint8_t)(seed + i);
        }
        return v;
    }
};

// Frame helper: after TrySendData, the visitor must have accepted exactly one
// frame whose handler covers [offset, offset+length).
std::shared_ptr<CryptoFrame> SingleFrame(DeliveryTestVisitor& v) {
    EXPECT_EQ(v.handled_.size(), 1u);
    return v.handled_.front();
}

TEST(CryptoStreamDeliveryTest, SendQueuesAndTrySendEmitsWithHandler) {
    Fixture f;
    f.Send(kHandshake, Fixture::Bytes(100, 1));
    EXPECT_TRUE(f.stream->HasPendingDataAt(kHandshake));

    DeliveryTestVisitor v;
    v.stream_allowance_ = 100;
    auto result = f.stream->TrySendData(&v, kHandshake);
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);

    auto frame = SingleFrame(v);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->GetOffset(), 0u);
    EXPECT_EQ(frame->GetLength(), 100u);
    EXPECT_TRUE(frame->HasDeliveryHandler()) << "CRYPTO frames must carry the delivery handler";
    EXPECT_FALSE(f.stream->HasPendingDataAt(kHandshake)) << "fully drained segment must leave the queue";
}

TEST(CryptoStreamDeliveryTest, PacketCapSplitsSegmentOffsets) {
    // The build-time budget path sizes frames via GetPacketLeftSize; segments
    // must split with consecutive offsets and re-emitted content.
    Fixture f;
    auto bytes = Fixture::Bytes(100, 7);
    f.Send(kHandshake, bytes);

    DeliveryTestVisitor v1;
    v1.packet_left_ = 20 + 40;  // reserve 20 header, 40 payload
    v1.stream_allowance_ = 40;
    ASSERT_EQ(f.stream->TrySendData(&v1, kHandshake), IStream::TrySendResult::kSuccess);
    auto f1 = SingleFrame(v1);
    EXPECT_EQ(f1->GetOffset(), 0u);
    EXPECT_EQ(f1->GetLength(), 40u);
    EXPECT_TRUE(f.stream->HasPendingDataAt(kHandshake));

    DeliveryTestVisitor v2;
    v2.stream_allowance_ = 1000;
    ASSERT_EQ(f.stream->TrySendData(&v2, kHandshake), IStream::TrySendResult::kSuccess);
    auto f2 = SingleFrame(v2);
    EXPECT_EQ(f2->GetOffset(), 40u) << "second emission must resume at the split offset";
    EXPECT_EQ(f2->GetLength(), 60u);
    EXPECT_TRUE(std::memcmp(f1->GetData().GetStart(), bytes.data(), 40) == 0);
    EXPECT_TRUE(std::memcmp(f2->GetData().GetStart(), bytes.data() + 40, 60) == 0);
}

TEST(CryptoStreamDeliveryTest, LostRequeuesRangeForResend) {
    Fixture f;
    auto bytes = Fixture::Bytes(100, 3);
    f.Send(kHandshake, bytes);

    DeliveryTestVisitor v;
    v.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
    auto first = SingleFrame(v);
    EXPECT_FALSE(f.stream->HasPendingDataAt(kHandshake));

    // SendControl fires kLost on the frame it tracked.
    first->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_TRUE(f.stream->HasPendingDataAt(kHandshake)) << "lost range must come back as an outgoing segment";

    DeliveryTestVisitor v2;
    v2.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v2, kHandshake), IStream::TrySendResult::kSuccess);
    auto resent = SingleFrame(v2);
    EXPECT_EQ(resent->GetOffset(), 0u);
    EXPECT_EQ(resent->GetLength(), 100u);
    EXPECT_TRUE(std::memcmp(resent->GetData().GetStart(), bytes.data(), 100) == 0);
}

TEST(CryptoStreamDeliveryTest, DuplicateLostIsIdempotent) {
    // PTO re-queue (SendControl::OnPTOTimer) can declare the same packet lost
    // twice; the second kLost must not enqueue a duplicate range.
    Fixture f;
    f.Send(kHandshake, Fixture::Bytes(100, 5));

    DeliveryTestVisitor v;
    v.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
    auto frame = SingleFrame(v);

    frame->NotifyDelivery(FrameDeliveryState::kLost);
    frame->NotifyDelivery(FrameDeliveryState::kLost);
    frame->NotifyDelivery(FrameDeliveryState::kLost);

    // Drain everything that was re-queued; duplicates would surface as extra
    // emissions of the same offset range.
    int emissions = 0;
    while (f.stream->HasPendingDataAt(kHandshake)) {
        DeliveryTestVisitor dv;
        dv.stream_allowance_ = 1000;
        ASSERT_EQ(f.stream->TrySendData(&dv, kHandshake), IStream::TrySendResult::kSuccess);
        ASSERT_EQ(dv.handled_.size(), 1u);
        EXPECT_EQ(dv.handled_.front()->GetOffset(), 0u);
        ++emissions;
    }
    EXPECT_EQ(emissions, 1) << "duplicate kLost must not enqueue duplicate ranges";
}

TEST(CryptoStreamDeliveryTest, AckedAdvancesWatermarkAndSuppressesLost) {
    Fixture f;
    f.Send(kHandshake, Fixture::Bytes(100, 9));

    DeliveryTestVisitor v;
    v.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
    auto frame = SingleFrame(v);

    // In-order life: ACK first, then a stale kLost (e.g. re-queued packet
    // finally reported lost after its replacement was acknowledged).
    frame->NotifyDelivery(FrameDeliveryState::kAcked);
    frame->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_FALSE(f.stream->HasPendingDataAt(kHandshake)) << "kLost covered by the acked prefix must be ignored";
}

TEST(CryptoStreamDeliveryTest, LateAckAfterLostPrunesResend) {
    // Out-of-order notifications: OnPacketAck fires kAcked on packets that
    // were already declared lost — the re-queued range must be retired.
    Fixture f;
    f.Send(kHandshake, Fixture::Bytes(100, 11));

    DeliveryTestVisitor v;
    v.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
    auto frame = SingleFrame(v);

    frame->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_TRUE(f.stream->HasPendingDataAt(kHandshake));
    frame->NotifyDelivery(FrameDeliveryState::kAcked);
    EXPECT_FALSE(f.stream->HasPendingDataAt(kHandshake)) << "late ACK must retire the re-queued segment";
}

TEST(CryptoStreamDeliveryTest, RetryResetClearsInitialOnly) {
    Fixture f;
    f.Send(kInitial, Fixture::Bytes(50, 1));
    f.Send(kHandshake, Fixture::Bytes(60, 2));

    DeliveryTestVisitor vi;
    vi.level_ = kInitial;
    vi.stream_allowance_ = 50;
    ASSERT_EQ(f.stream->TrySendData(&vi, kInitial), IStream::TrySendResult::kSuccess);

    f.stream->ResetForRetry();

    EXPECT_FALSE(f.stream->HasPendingDataAt(kInitial)) << "Retry kills the Initial-level flight";
    // Handshake level is untouched by the retry reset.
    DeliveryTestVisitor vh;
    vh.stream_allowance_ = 60;
    ASSERT_EQ(f.stream->TrySendData(&vh, kHandshake), IStream::TrySendResult::kSuccess);
    auto frame = SingleFrame(vh);
    EXPECT_EQ(frame->GetOffset(), 0u);
    EXPECT_EQ(frame->GetLength(), 60u);

    // Post-retry Initial data restarts at offset 0 (TLS regenerates the
    // flight); the stale kLost of the pre-retry frame must find nothing.
    f.Send(kInitial, Fixture::Bytes(30, 3));
    DeliveryTestVisitor vi2;
    vi2.level_ = kInitial;
    vi2.stream_allowance_ = 30;
    ASSERT_EQ(f.stream->TrySendData(&vi2, kInitial), IStream::TrySendResult::kSuccess);
    auto init_frame = SingleFrame(vi2);
    init_frame->NotifyDelivery(FrameDeliveryState::kLost);
    DeliveryTestVisitor vi3;
    vi3.level_ = kInitial;
    vi3.stream_allowance_ = 30;
    ASSERT_EQ(f.stream->TrySendData(&vi3, kInitial), IStream::TrySendResult::kSuccess);
    EXPECT_EQ(vi3.handled_.front()->GetOffset(), 0u) << "pre-retry offsets must not resurrect post-retry data";
}

TEST(CryptoStreamDeliveryTest, OversizedSendSpansChunksWithConsecutiveOffsets) {
    // A single Send() larger than one pool chunk must not drop bytes (the old
    // MultiBlockBuffer path could partial-write silently); it spans chunks
    // whose offsets concatenate.
    Fixture f;
    auto big = Fixture::Bytes(100000, 13);
    f.Send(kHandshake, big);

    uint64_t next_offset = 0;
    size_t total = 0;
    while (f.stream->HasPendingDataAt(kHandshake)) {
        DeliveryTestVisitor v;
        v.stream_allowance_ = 5000;
        ASSERT_EQ(f.stream->TrySendData(&v, kHandshake), IStream::TrySendResult::kSuccess);
        ASSERT_EQ(v.handled_.size(), 1u);
        auto frame = v.handled_.front();
        EXPECT_EQ(frame->GetOffset(), next_offset) << "chunk-spanning segments must concatenate offsets";
        next_offset += frame->GetLength();
        total += frame->GetLength();
    }
    EXPECT_EQ(total, big.size());
}

// Regression for the HelloRequest bug (2026-09-19): TLS may hand a flight to
// the crypto stream in MULTIPLE Send() calls (e.g. an NST header of 4 B then
// its 210 B body), creating several retained segments. When a frame covering
// the tail of the second chunk is lost, the re-queue lookup must resolve to
// the chunk that actually HOLDS those bytes. The buggy range lookup used
// chunk->GetLength() — the pool-block CAPACITY, not the bytes written — so
// the first segment's range was inflated far past its real extent, the lost
// range resolved against the WRONG chunk, and the re-emission read
// uninitialized pool memory (on-wire symptom: CRYPTO frames with
// "6a6a0000..." heads, which rustls parses as HelloRequest).
TEST(CryptoStreamDeliveryTest, MultiSendLostTailRangeReadsCorrectChunk) {
    Fixture f;
    // Two Send() calls, like a TLS flight arriving in pieces: [0,4) then [4,214).
    auto part1 = Fixture::Bytes(4, 0xA1);
    auto part2 = Fixture::Bytes(210, 0xB2);
    f.Send(kHandshake, part1);
    f.Send(kHandshake, part2);

    // Each Send() is its own outgoing segment: first the tiny [0,4) frame...
    DeliveryTestVisitor vhead;
    vhead.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&vhead, kHandshake), IStream::TrySendResult::kSuccess);
    ASSERT_EQ(vhead.handled_.front()->GetOffset(), 0u);
    ASSERT_EQ(vhead.handled_.front()->GetLength(), 4u);

    // ...then the 210 B segment, split by a packet-budget cap into [4,210)
    // and the 4-byte tail [210,214) — the tail frame is the one that goes
    // missing in the wild.
    DeliveryTestVisitor vcap;
    vcap.packet_left_ = 20 + 206;
    vcap.stream_allowance_ = 206;
    ASSERT_EQ(f.stream->TrySendData(&vcap, kHandshake), IStream::TrySendResult::kSuccess);
    ASSERT_EQ(vcap.handled_.front()->GetOffset(), 4u);
    ASSERT_EQ(vcap.handled_.front()->GetLength(), 206u);

    DeliveryTestVisitor vtail;
    vtail.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&vtail, kHandshake), IStream::TrySendResult::kSuccess);
    auto frame = SingleFrame(vtail);
    ASSERT_EQ(frame->GetOffset(), 210u);
    ASSERT_EQ(frame->GetLength(), 4u);

    // The correct bytes are the last 4 of part2; the buggy lookup resolved
    // this against part1's chunk (capacity-inflated range) and read garbage.
    frame->NotifyDelivery(FrameDeliveryState::kLost);

    DeliveryTestVisitor vresend;
    vresend.stream_allowance_ = 1000;
    ASSERT_EQ(f.stream->TrySendData(&vresend, kHandshake), IStream::TrySendResult::kSuccess);
    auto resent = SingleFrame(vresend);
    EXPECT_EQ(resent->GetOffset(), 210u);
    ASSERT_EQ(resent->GetLength(), 4u);
    const uint8_t* got = resent->GetData().GetStart();
    // The correct bytes are the last 4 of part2; garbage (uninitialized pool
    // memory, e.g. 6a 6a 00 00) means the lookup resolved to part1's chunk.
    std::vector<uint8_t> expect(part2.end() - 4, part2.end());
    EXPECT_TRUE(std::memcmp(got, expect.data(), 4) == 0)
        << "re-emitted tail must come from the chunk that holds it, got: "
        << std::hex << (int)got[0] << " " << (int)got[1] << " " << (int)got[2] << " " << (int)got[3];
}

// =====================================================================
// Code review round 2 fixes.
// =====================================================================

// #1 (P1): a full packet build buffer (packet_left < 20B header reserve =>
// crypto_pkt_cap == 0) must defer the segment (kBreak), not pop it. The old
// code silently dropped handshake bytes here, hanging the connection.
TEST(CryptoStreamDeliveryTest, PacketFullDefersSegmentInsteadOfDropping) {
    Fixture f;
    auto bytes = Fixture::Bytes(100, 21);
    f.Send(kHandshake, bytes);
    ASSERT_TRUE(f.stream->HasPendingDataAt(kHandshake));

    DeliveryTestVisitor v;
    v.packet_left_ = 10;  // < kCryptoHeaderReserve(20) => crypto_pkt_cap == 0
    v.stream_allowance_ = 100;
    auto result = f.stream->TrySendData(&v, kHandshake);
    EXPECT_EQ(result, IStream::TrySendResult::kBreak)
        << "A full packet must defer the segment for the next packet, not drop it.";
    EXPECT_TRUE(f.stream->HasPendingDataAt(kHandshake)) << "the segment must still be queued";
    EXPECT_TRUE(v.handled_.empty()) << "no frame may be emitted with zero budget";

    // And the deferred segment survives intact: a later call with budget
    // emits it from offset 0.
    DeliveryTestVisitor v2;
    v2.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v2, kHandshake), IStream::TrySendResult::kSuccess);
    auto frame = SingleFrame(v2);
    EXPECT_EQ(frame->GetOffset(), 0u);
    EXPECT_EQ(frame->GetLength(), 100u);
    EXPECT_TRUE(std::memcmp(frame->GetData().GetStart(), bytes.data(), 100) == 0);
}

// #2 (P2): kAcked must trim the fully-acked prefix of retained_. Without it,
// every retained chunk on Handshake/1-RTT (one pool block per Send() slice,
// e.g. every NewSessionTicket) stayed alive forever.
TEST(CryptoStreamDeliveryTest, AckedTrimsRetainedPrefix) {
    Fixture f;
    // Three Send() slices -> three retained segments at strictly increasing
    // offsets.
    f.Send(kHandshake, Fixture::Bytes(100, 1));  // [0,100)
    f.Send(kHandshake, Fixture::Bytes(100, 2));  // [100,200)
    f.Send(kHandshake, Fixture::Bytes(100, 3));  // [200,300)
    ASSERT_EQ(f.stream->GetRetainedSegmentCountForTest(kHandshake), 3u);

    // Emit the first two slices and ACK them.
    DeliveryTestVisitor v1;
    v1.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v1, kHandshake), IStream::TrySendResult::kSuccess);
    SingleFrame(v1)->NotifyDelivery(FrameDeliveryState::kAcked);
    DeliveryTestVisitor v2;
    v2.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v2, kHandshake), IStream::TrySendResult::kSuccess);
    SingleFrame(v2)->NotifyDelivery(FrameDeliveryState::kAcked);

    EXPECT_EQ(f.stream->GetRetainedSegmentCountForTest(kHandshake), 1u)
        << "acked prefix ([0,200)) must be trimmed from retained_";
}

// #4 (P3): a partially-overlapping kLost must re-queue only the uncovered
// difference. The old full-containment check re-queued the already-queued
// prefix too (wasted 8.1 budget).
TEST(CryptoStreamDeliveryTest, PartialOverlapRequeuesOnlyDifference) {
    Fixture f;
    auto bytes = Fixture::Bytes(300, 31);
    f.Send(kHandshake, bytes);  // one segment [0,300)

    // Emit [0,100) (frame f1); the segment's remainder [100,300) stays queued.
    DeliveryTestVisitor v1;
    v1.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v1, kHandshake), IStream::TrySendResult::kSuccess);
    auto f1 = SingleFrame(v1);
    ASSERT_EQ(f1->GetOffset(), 0u);
    ASSERT_EQ(f1->GetLength(), 100u);

    // f1 is declared lost: the uncovered [0,100) is re-queued at the BACK
    // (re-queues push_back); the queue holds [100,300) then [0,100).
    f1->NotifyDelivery(FrameDeliveryState::kLost);
    ASSERT_EQ(f.stream->GetOutgoingSegmentCountForTest(kHandshake), 2u);

    // Drain the queue front first ([100,300)), then part of the re-queued
    // [0,100): emit [0,50), leaving the re-queued segment's remainder [50,100)
    // as the only queued range.
    DeliveryTestVisitor v2;
    v2.stream_allowance_ = 200;
    ASSERT_EQ(f.stream->TrySendData(&v2, kHandshake), IStream::TrySendResult::kSuccess);  // [100,300)
    DeliveryTestVisitor v3;
    v3.stream_allowance_ = 50;
    ASSERT_EQ(f.stream->TrySendData(&v3, kHandshake), IStream::TrySendResult::kSuccess);  // [0,50)
    auto f3 = SingleFrame(v3);
    ASSERT_EQ(f3->GetOffset(), 0u);
    ASSERT_EQ(f3->GetLength(), 50u);
    ASSERT_EQ(f.stream->GetOutgoingSegmentCountForTest(kHandshake), 1u);  // [50,100)

    // A second kLost for the SAME frame f1 ([0,100)) now PARTIALLY overlaps
    // the queued [50,100) remainder. Only the uncovered [0,50) may be
    // re-queued: the old full-containment check ([50,100) does not contain
    // [0,100)) re-queued the whole range, duplicating [50,100).
    f1->NotifyDelivery(FrameDeliveryState::kLost);

    // Drain: [50,100) first, then exactly [0,50). A 100-byte final emission
    // means the difference logic regressed to full-range re-queue.
    std::vector<std::pair<uint64_t, uint32_t>> emissions;
    while (f.stream->HasPendingDataAt(kHandshake)) {
        DeliveryTestVisitor dv;
        dv.stream_allowance_ = 1000;
        ASSERT_EQ(f.stream->TrySendData(&dv, kHandshake), IStream::TrySendResult::kSuccess);
        ASSERT_EQ(dv.handled_.size(), 1u);
        emissions.emplace_back(dv.handled_.front()->GetOffset(), dv.handled_.front()->GetLength());
    }
    ASSERT_EQ(emissions.size(), 2u);
    EXPECT_EQ(emissions[0].first, 50u);  // queued remainder first
    EXPECT_EQ(emissions[0].second, 50u);
    EXPECT_EQ(emissions[1].first, 0u);  // only the uncovered difference
    EXPECT_EQ(emissions[1].second, 50u) << "must re-queue only [0,50); a 100-byte emission here means "
                                           "the old full-range re-queue is back.";
}

// #3 regression: after the end_offset refactor, a mid-chunk re-queue must
// emit exactly the lost bytes (offset/length/content alignment). This is the
// scenario that once produced the underflow + uninitialized-pool-memory
// CRYPTO frames.
TEST(CryptoStreamDeliveryTest, MidChunkRequeueEmitsExactRange) {
    Fixture f;
    auto bytes = Fixture::Bytes(300, 41);
    f.Send(kHandshake, bytes);  // [0,300)

    // Emit [0,100), leaving [100,300) queued.
    DeliveryTestVisitor v1;
    v1.stream_allowance_ = 100;
    ASSERT_EQ(f.stream->TrySendData(&v1, kHandshake), IStream::TrySendResult::kSuccess);
    auto f1 = SingleFrame(v1);

    // f1 is lost: [0,100) is re-queued at the BACK. Drain the front [100,300)
    // first, then part of the re-queued segment so its remainder starts
    // MID-CHUNK (pos=60) — the pre-#3 length/pos mismatch underflowed seg_left
    // here and emitted a cap-sized frame of uninitialized pool memory.
    f1->NotifyDelivery(FrameDeliveryState::kLost);  // re-queues [0,100)
    DeliveryTestVisitor v2;
    v2.stream_allowance_ = 200;
    ASSERT_EQ(f.stream->TrySendData(&v2, kHandshake), IStream::TrySendResult::kSuccess);  // [100,300)
    DeliveryTestVisitor v3;
    v3.stream_allowance_ = 60;
    ASSERT_EQ(f.stream->TrySendData(&v3, kHandshake), IStream::TrySendResult::kSuccess);  // [0,60)
    auto f3 = SingleFrame(v3);
    ASSERT_EQ(f3->GetOffset(), 0u);
    ASSERT_EQ(f3->GetLength(), 60u);
    EXPECT_TRUE(std::memcmp(f3->GetData().GetStart(), bytes.data(), 60) == 0) << "content must match the source bytes";

    // The remainder [60,100) of the re-queued segment starts MID-CHUNK.
    // Emitting it must produce exactly offset=60, length=40 with the matching
    // bytes.
    DeliveryTestVisitor v4;
    v4.stream_allowance_ = 1000;
    ASSERT_EQ(f.stream->TrySendData(&v4, kHandshake), IStream::TrySendResult::kSuccess);
    auto f4 = SingleFrame(v4);
    EXPECT_EQ(f4->GetOffset(), 60u);
    EXPECT_EQ(f4->GetLength(), 40u) << "mid-chunk remainder must emit exactly its remaining bytes";
    EXPECT_TRUE(std::memcmp(f4->GetData().GetStart(), bytes.data() + 60, 40) == 0)
        << "mid-chunk remainder must emit the source bytes, not uninitialized pool memory";
}

}  // namespace
}  // namespace quic
}  // namespace quicx
