#include <gtest/gtest.h>
#include <cinttypes>
#include <cstdint>


#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/reno_congestion_control.h"

using quicx::quic::AckEvent;
using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::LossEvent;
using quicx::quic::RenoCongestionControl;
using quicx::quic::SentPacketEvent;

TEST(RenoCongestionControlTest, InitialState) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1200;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes);
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    EXPECT_TRUE(cc.InSlowStart());
    EXPECT_FALSE(cc.InRecovery());
}

TEST(RenoCongestionControlTest, SlowStartIncreasesCwndOnAck) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Send and ACK 2k bytes
    SentPacketEvent sp{1, 2000, 100, false};
    cc.OnPacketSent(sp);
    EXPECT_EQ(cc.GetBytesInFlight(), 2000u);

    AckEvent ack{1, 2000, 200, 0, false};
    cc.OnPacketAcked(ack);
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    // Slow start: cwnd += acked
    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes + 2000);
}

TEST(RenoCongestionControlTest, LossEntersRecoveryAndReducesCwnd) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;  // 20000
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    uint64_t before = cc.GetCongestionWindow();
    // Report a loss of 1000 bytes at time 150
    LossEvent le{2, 1000, 150};
    cc.OnPacketLost(le);

    uint64_t expected_ssthresh = static_cast<uint64_t>(before * cfg.beta);
    if (expected_ssthresh < cfg.min_cwnd_bytes) expected_ssthresh = cfg.min_cwnd_bytes;
    EXPECT_EQ(cc.GetSsthresh(), expected_ssthresh);
    EXPECT_EQ(cc.GetCongestionWindow(), expected_ssthresh);
    EXPECT_FALSE(cc.InSlowStart());
    EXPECT_TRUE(cc.InRecovery());
}

TEST(RenoCongestionControlTest, CongestionAvoidanceGrowthAfterRecovery) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    // Enter recovery via loss
    cc.OnPacketLost(LossEvent{3, 1000, 100});
    uint64_t cwnd_after_loss = cc.GetCongestionWindow();
    EXPECT_TRUE(cc.InRecovery());

    // ACK after recovery start: per RFC 9002 §7.3.2, recovery exits only when
    // the ACK is for a packet whose SEND time is after recovery_start_time_.
    // Here recovery_start_time_=100, so the acked packet must have send_time > 100.
    uint64_t acked = 1000;
    AckEvent post_recovery_ack{};
    post_recovery_ack.pn = 3;
    post_recovery_ack.bytes_acked = acked;
    post_recovery_ack.ack_time = 200;
    post_recovery_ack.ack_delay = 0;
    post_recovery_ack.ecn_ce = false;
    post_recovery_ack.acked_packet_send_time = 150;  // > recovery_start_time_ (100)
    cc.OnPacketAcked(post_recovery_ack);
    EXPECT_FALSE(cc.InRecovery());

    // In CA: cwnd increases by roughly MSS^2 / cwnd (at least 1)
    uint64_t cwnd_after_ack = cc.GetCongestionWindow();
    EXPECT_GE(cwnd_after_ack, cwnd_after_loss + 1);
}

TEST(RenoCongestionControlTest, CanSendReportsCorrectly) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 5000;
    cc.Configure(cfg);

    uint64_t can_send = 0;
    // Initially no in-flight, should be able to send up to cwnd
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kOk);
    EXPECT_EQ(can_send, cfg.initial_cwnd_bytes);

    // Simulate sending 5k: in-flight == cwnd
    cc.OnPacketSent(SentPacketEvent{10, cfg.initial_cwnd_bytes, 10, false});
    EXPECT_EQ(cc.GetBytesInFlight(), cfg.initial_cwnd_bytes);
    can_send = 1234;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(can_send, 0u);
}

TEST(RenoCongestionControlTest, PacingRateComputesFromSrtt) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;  // 10000
    cc.Configure(cfg);

    // SRTT = 100000us (100ms)
    cc.OnRoundTripSample(100000, 0);
    uint64_t expected_bps = (cfg.initial_cwnd_bytes * 8ull * 1000000ull) / 100000ull;
    EXPECT_EQ(cc.GetPacingRateBps(), expected_bps);
}

TEST(RenoCongestionControlTest, RecoveryShrinksCwndAndAcksRestoreSendCapacity) {
    // RFC 9002 §7.3.2: after entering recovery, cwnd is reduced to ssthresh
    // (cwnd*beta) regardless of bytes_in_flight. It is EXPECTED and CORRECT
    // for bytes_in_flight to temporarily exceed cwnd until ACKs drain it.
    // CanSend blocks new sends in the meantime.
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;  // 14600
    cfg.beta = 0.5;
    cc.Configure(cfg);

    // Simulate slow start growth
    uint64_t now = 1000;
    for (int i = 0; i < 100; i++) {
        cc.OnPacketSent(SentPacketEvent{static_cast<uint64_t>(i), 1460, now, false});
        now += 1;

        AckEvent ack{};
        ack.pn = static_cast<uint64_t>(i);
        ack.bytes_acked = 1460;
        ack.ack_time = now;
        ack.acked_packet_send_time = now - 1;
        cc.OnPacketAcked(ack);
        cc.OnRoundTripSample(10, 0);
        now += 1;
    }

    // Send many packets without ACKing to push bytes_in_flight up to cwnd.
    for (int i = 100; i < 250; i++) {
        uint64_t can_send = 0;
        if (cc.CanSend(now, can_send) == ICongestionControl::SendState::kOk && can_send >= 1460) {
            cc.OnPacketSent(SentPacketEvent{static_cast<uint64_t>(i), 1460, now, false});
            now += 1;
        }
    }

    uint64_t bytes_in_flight_before_loss = cc.GetBytesInFlight();
    uint64_t cwnd_before_loss = cc.GetCongestionWindow();
    ASSERT_GT(bytes_in_flight_before_loss, 0u);

    // Trigger packet loss
    cc.OnPacketLost(LossEvent{150, 1460, now});

    uint64_t cwnd_after_loss = cc.GetCongestionWindow();
    uint64_t bytes_in_flight_after_loss = cc.GetBytesInFlight();

    // RFC 9002: cwnd should be approximately cwnd_before_loss * beta (0.5),
    // bounded below by min_cwnd. It should NOT be raised back to in_flight.
    uint64_t expected_cwnd = static_cast<uint64_t>(cwnd_before_loss * cfg.beta);
    if (expected_cwnd < cfg.min_cwnd_bytes) expected_cwnd = cfg.min_cwnd_bytes;
    EXPECT_EQ(cwnd_after_loss, expected_cwnd) << "cwnd must shrink to ssthresh, not be raised to in_flight";

    // While in recovery, CanSend should report blocked because in_flight > cwnd.
    uint64_t can_send = 0;
    auto state = cc.CanSend(now, can_send);
    if (bytes_in_flight_after_loss >= cwnd_after_loss) {
        EXPECT_EQ(state, ICongestionControl::SendState::kBlockedByCwnd);
        EXPECT_EQ(can_send, 0u);
    }

    // ACKs (with send_time before recovery start, so still in recovery) drain in_flight
    // without growing cwnd; eventually CanSend opens up.
    for (int i = 100; i < 200; i++) {
        AckEvent ack{};
        ack.pn = static_cast<uint64_t>(i);
        ack.bytes_acked = 1460;
        ack.ack_time = now;
        ack.acked_packet_send_time = 0;  // sent before recovery: stays in recovery
        cc.OnPacketAcked(ack);
        now += 1;
    }

    state = cc.CanSend(now, can_send);
    EXPECT_GT(can_send, 0u) << "Should be able to send after ACKs drain bytes_in_flight";
}

// ====================================================================
// G2 (Bug #22) hypothesis tests — cwnd-stuck-near-bytes-in-flight
// ====================================================================
//
// Symptom observed in interop logs (quicx server vs quiche/quic-go client):
// CanSend() reports max_bytes(cwnd) cycling in the 1-31 byte range for many
// seconds, even though conn flow-control is wide open. The connection then
// stalls until idle timeout. Stream frame headers need ~6-20 bytes, so any
// cwnd-headroom < ~32B cannot emit a usable packet, so OnPacketSent is never
// called, so bytes_in_flight never grows past cwnd, so CanSend cycles forever.
//
// These tests isolate the algorithm-layer behaviours that *could* keep
// (cwnd - bytes_in_flight) pinned at a tiny positive value. If any of them
// fail, that's the bug. If all pass, the bug lives above the CC layer
// (send_control packet tracking, pacing, or stream-frame builder).
//
// Numbers below mirror the real interop scenario where reproducible:
//   - mss = 1460 (default)
//   - initial cwnd = 14600 (10*MSS)
//   - beta = 0.5
//   - many sends accumulate, then an ACK pattern triggers loss declarations.

// H1: After OnPacketLost decrements bytes_in_flight, an ACK for the SAME pn
// must NOT also decrement bytes_in_flight. (send_control guards this with
// `is_lost`, but if anything routes a stray AckEvent for a lost pn into the
// CC, in_flight will go below the truth and cwnd recovery math will be off.)
// At the algorithm layer, the CC has no pn memory, so it would happily double
// decrement. This test documents that contract; if it ever fails it means
// someone added pn-dedup logic and we can stop relying on send_control's
// guard.
TEST(RenoCongestionControlTest, G2_LossThenSpuriousAckDoubleDecrement_Documented) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    cc.OnPacketSent(SentPacketEvent{1, 1460, 100, false});
    cc.OnPacketSent(SentPacketEvent{2, 1460, 101, false});
    EXPECT_EQ(cc.GetBytesInFlight(), 2u * 1460u);

    cc.OnPacketLost(LossEvent{1, 1460, 200});
    EXPECT_EQ(cc.GetBytesInFlight(), 1460u) << "loss should drop in_flight by lost bytes";

    // Spurious AckEvent for the same lost pn=1: at the CC layer this WILL
    // double-decrement (CC has no pn dedup). This documents the contract:
    // send_control MUST never call OnPacketAcked for a packet it already
    // reported via OnPacketLost.
    AckEvent stray{};
    stray.pn = 1;
    stray.bytes_acked = 1460;
    stray.ack_time = 300;
    stray.acked_packet_send_time = 100;
    cc.OnPacketAcked(stray);
    // Today: in_flight floors at 0 via saturating subtract, so we would see 0
    // here even though the real in-flight is 1460 (pn=2 still unacked).
    // Expectation locked: contract violated by caller -> in_flight underflow.
    EXPECT_EQ(cc.GetBytesInFlight(), 0u)
        << "Algorithm layer has no pn dedup; double-decrement floors at 0. "
           "If this changes, the send_control guard can be relaxed.";
}

// H2: When an ACK arrives whose acked_packet_send_time was filled in but the
// peer's ACK delay caused us to enter recovery just before processing it
// (e.g. a loss declared by DetectLostPackets earlier in the same OnPacketAck
// frame), the recovery_start_time_ may be > acked_packet_send_time even for
// packets that were sent AFTER the bulk of the loss. In that case the CC
// stays stuck in recovery (correct per RFC) but cwnd MUST still drop in_flight
// on every subsequent ACK so that the window eventually opens up. Verify that
// the in_flight bookkeeping in recovery is correct.
TEST(RenoCongestionControlTest, G2_RecoveryAcksMustDrainInFlightEvenWithoutCwndGrowth) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;  // 146000
    cfg.beta = 0.5;
    cc.Configure(cfg);

    // Send 100 packets back-to-back to fill in_flight close to cwnd.
    uint64_t now = 1000;
    for (uint64_t i = 1; i <= 100; ++i) {
        cc.OnPacketSent(SentPacketEvent{i, 1460, now, false});
        now += 1;
    }
    uint64_t in_flight_before = cc.GetBytesInFlight();
    ASSERT_EQ(in_flight_before, 100u * 1460u);

    // Loss is declared at t=200 for pn=1: this enters recovery, halves cwnd,
    // and decrements in_flight by 1460.
    uint64_t loss_time = 200;
    cc.OnPacketLost(LossEvent{1, 1460, loss_time});
    ASSERT_TRUE(cc.InRecovery());
    uint64_t cwnd_in_recovery = cc.GetCongestionWindow();
    EXPECT_EQ(cwnd_in_recovery, static_cast<uint64_t>(cfg.initial_cwnd_bytes * cfg.beta));
    EXPECT_EQ(cc.GetBytesInFlight(), 99u * 1460u);

    // Now ACK pn=2..50 (all sent BEFORE loss_time, so recovery does NOT exit).
    // Each ACK must decrement in_flight by 1460 even though cwnd is frozen.
    for (uint64_t i = 2; i <= 50; ++i) {
        AckEvent ack{};
        ack.pn = i;
        ack.bytes_acked = 1460;
        ack.ack_time = 300;
        ack.acked_packet_send_time = 1000 + (i - 1);  // < loss_time? send_time was 1000+i-1, but loss_time was 200; we INTENTIONALLY make these <= loss_time so recovery does NOT exit.
        // Wait: 1000+(i-1) is always >>200, so this would actually exit recovery.
        // To keep recovery, set send_time to something < loss_time.
        ack.acked_packet_send_time = 0;  // 0 < 200 = recovery_start_time -> stays in recovery
        cc.OnPacketAcked(ack);
    }
    EXPECT_TRUE(cc.InRecovery()) << "send_time=0 < recovery_start=200 must keep us in recovery";
    EXPECT_EQ(cc.GetBytesInFlight(), (99u - 49u) * 1460u)
        << "ACKs in recovery MUST still drain in_flight";
    EXPECT_EQ(cc.GetCongestionWindow(), cwnd_in_recovery)
        << "ACKs in recovery MUST NOT grow cwnd";

    // CanSend should now report a healthy window: cwnd=73000, in_flight=73000.
    // Actually 50 packets at 1460 each = 73000, and cwnd halved to 73000.
    // So left=0 and we are still cwnd-blocked. ACK pn=51..70 to open up.
    for (uint64_t i = 51; i <= 70; ++i) {
        AckEvent ack{};
        ack.pn = i;
        ack.bytes_acked = 1460;
        ack.ack_time = 400;
        ack.acked_packet_send_time = 0;  // stay in recovery
        cc.OnPacketAcked(ack);
    }
    uint64_t left = 0;
    auto state = cc.CanSend(500, left);
    EXPECT_EQ(state, ICongestionControl::SendState::kOk);
    EXPECT_GE(left, 1460u) << "After draining in_flight via ACKs, cwnd window must reopen";
}

// H3: This is the suspected G2 bug fingerprint. After many rounds of
// loss-then-partial-recovery, does cwnd ever land at a value just barely
// above bytes_in_flight, such that CanSend reports a tiny non-zero window
// (1-31 bytes) for an extended period?
//
// Construction: mimic the interop-observed pattern of "every ACK arrival
// declares 1-2 packets as lost". Each loss halves cwnd; each ACK in recovery
// drains a small amount of in_flight. If the math is right, cwnd should
// either stay at min_cwnd_bytes (2*MSS = 2920) or open up — never spend any
// time in (cwnd - in_flight) ∈ [1, MSS).
TEST(RenoCongestionControlTest, G2_RepeatedLossDoesNotStrandCwndAtSubMSSGap) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 200 * cfg.mss_bytes;  // 292000 — matches log
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;        // 2920
    cfg.beta = 0.5;
    cc.Configure(cfg);

    // Phase 1: fill in_flight to nearly cwnd.
    uint64_t now = 1000;
    uint64_t next_pn = 1;
    auto fill_to_cwnd = [&]() {
        while (true) {
            uint64_t left = 0;
            auto st = cc.CanSend(now, left);
            if (st != ICongestionControl::SendState::kOk || left < cfg.mss_bytes) break;
            cc.OnPacketSent(SentPacketEvent{next_pn++, cfg.mss_bytes, now, false});
            now += 1;
        }
    };
    fill_to_cwnd();
    ASSERT_GT(cc.GetBytesInFlight(), 100u * 1460u);

    // Phase 2: simulate the interop pattern. Each iteration:
    //   (a) declare 1 packet lost  -> EnterRecovery (or stay)
    //   (b) ACK 2 packets (sent before recovery start) -> drain in_flight
    //   (c) measure cwnd-in_flight gap; it MUST be 0 or >= MSS, never tiny.
    uint64_t lowest_unacked_pn = 1;
    int sub_mss_gap_observations = 0;
    for (int round = 0; round < 50; ++round) {
        cc.OnPacketLost(LossEvent{lowest_unacked_pn, cfg.mss_bytes, now});
        ++lowest_unacked_pn;
        for (int k = 0; k < 2 && lowest_unacked_pn < next_pn; ++k) {
            AckEvent ack{};
            ack.pn = lowest_unacked_pn++;
            ack.bytes_acked = cfg.mss_bytes;
            ack.ack_time = now;
            ack.acked_packet_send_time = 0;  // stay in recovery
            cc.OnPacketAcked(ack);
        }
        // Inspect the gap.
        uint64_t cw = cc.GetCongestionWindow();
        uint64_t inf = cc.GetBytesInFlight();
        uint64_t gap = (cw > inf) ? (cw - inf) : 0;
        if (gap > 0 && gap < cfg.mss_bytes) {
            ++sub_mss_gap_observations;
            // Surface the first occurrence in the test log for diagnosis.
            if (sub_mss_gap_observations == 1) {
                ::testing::Test::RecordProperty("first_sub_mss_round",
                    std::to_string(round));
                ::testing::Test::RecordProperty("first_sub_mss_cwnd",
                    std::to_string(cw));
                ::testing::Test::RecordProperty("first_sub_mss_inflight",
                    std::to_string(inf));
                ::testing::Test::RecordProperty("first_sub_mss_gap",
                    std::to_string(gap));
            }
        }
        now += 10;
        // Try to send a follow-on packet using the gap; if gap < MSS we
        // would NOT send (matches send_manager behaviour), so in_flight
        // does NOT grow back. This mirrors the production stall.
        if (gap >= cfg.mss_bytes) {
            cc.OnPacketSent(SentPacketEvent{next_pn++, cfg.mss_bytes, now, false});
        }
    }

    // The whole point of this test: at the algorithm layer, repeated loss
    // halving against a high in_flight should NEVER produce sub-MSS gaps
    // — cwnd halves to a multiple of MSS-ish range and either stays >= in_flight
    // (window-open) or stays below in_flight (fully cwnd-blocked, gap=0).
    EXPECT_EQ(sub_mss_gap_observations, 0)
        << "Algorithm layer produced sub-MSS cwnd gap " << sub_mss_gap_observations
        << " times. If >0, this IS the G2 bug at the CC layer. If 0, the bug "
           "is in send_control (e.g. spurious double-decrement of in_flight, "
           "or partial-byte sends crediting in_flight) and these tests can't "
           "see it.";
}

// H4: cwnd must never go below cfg.min_cwnd_bytes regardless of how many
// consecutive losses we declare. The interop logs showed cwnd values of
// 1, 4, 11 etc. at CanSend time; but that's (cwnd - in_flight), not cwnd
// itself. Verify the floor explicitly.
TEST(RenoCongestionControlTest, G2_CwndNeverDropsBelowMinAfterRepeatedLosses) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    // Send once so loss has something to decrement.
    for (uint64_t i = 1; i <= 200; ++i) {
        cc.OnPacketSent(SentPacketEvent{i, cfg.mss_bytes, 100 + i, false});
    }

    // Declare 30 consecutive losses, never any ACK (so we'd repeatedly
    // re-enter recovery only on the FIRST one — once in_recovery_, EnterRecovery
    // is not called again). cwnd stays at the post-loss value.
    for (uint64_t i = 1; i <= 30; ++i) {
        cc.OnPacketLost(LossEvent{i, cfg.mss_bytes, 1000 + i});
        EXPECT_GE(cc.GetCongestionWindow(), cfg.min_cwnd_bytes)
            << "cwnd dropped below min after " << i << " losses";
    }
    EXPECT_TRUE(cc.InRecovery());
}

// H5: ECN-CE behaves like loss for cwnd reduction. If a stream of CE-marked
// ACKs keeps arriving, EnterRecovery should be called only ONCE per recovery
// epoch, not per CE-marked ACK. Otherwise cwnd halves on every ACK and
// collapses to min_cwnd quickly — would also produce the "tiny gap" symptom.
TEST(RenoCongestionControlTest, G2_RepeatedEcnCeDoesNotCollapseCwnd) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    for (uint64_t i = 1; i <= 50; ++i) {
        cc.OnPacketSent(SentPacketEvent{i, cfg.mss_bytes, 100 + i, false});
    }

    uint64_t cwnd_before_first_ce = cc.GetCongestionWindow();
    // First ECN-CE ACK halves cwnd, enters recovery.
    AckEvent ce{};
    ce.pn = 1;
    ce.bytes_acked = cfg.mss_bytes;
    ce.ack_time = 200;
    ce.ecn_ce = true;
    ce.acked_packet_send_time = 101;
    cc.OnPacketAcked(ce);
    uint64_t cwnd_after_first = cc.GetCongestionWindow();
    EXPECT_LT(cwnd_after_first, cwnd_before_first_ce);
    EXPECT_TRUE(cc.InRecovery());

    // Second/third/... CE ACKs (still in recovery) MUST NOT keep halving.
    for (uint64_t i = 2; i <= 10; ++i) {
        AckEvent ce_more{};
        ce_more.pn = i;
        ce_more.bytes_acked = cfg.mss_bytes;
        ce_more.ack_time = 200 + i;
        ce_more.ecn_ce = true;
        ce_more.acked_packet_send_time = 100 + i;  // still <= recovery start
        cc.OnPacketAcked(ce_more);
    }
    EXPECT_EQ(cc.GetCongestionWindow(), cwnd_after_first)
        << "Repeated CE-marked ACKs in same recovery epoch must not keep "
           "halving cwnd; that would mimic the G2 stall pattern.";
}
