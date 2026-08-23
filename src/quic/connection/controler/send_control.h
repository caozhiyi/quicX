#ifndef QUIC_CONNECTION_CONTROLER_SEND_CONTROL
#define QUIC_CONNECTION_CONTROLER_SEND_CONTROL

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include <quicx/common/if_timer_scheduler.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/connection/controler/rtt_calculator.h"
#include "quic/connection/transport_param.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

// Stream data info in a packet for ACK tracking.
//
// Each entry records exactly one STREAM frame's byte range so the
// stream-side ACK bookkeeping can do *selective* tracking instead of being
// fooled by a single high cumulative offset.  When the same packet contains
// several STREAM frames for the same stream (rare, but legal — e.g. a small
// retransmitted gap followed by fresh data), we keep one StreamDataInfo per
// frame rather than collapsing them, otherwise [offset_start, offset_start +
// length) would lose meaning.
struct StreamDataInfo {
    uint64_t stream_id;
    uint64_t offset_start;  // Offset of the first byte of this stream frame
    uint64_t length;        // Length of the stream frame payload (0 for FIN-only)
    bool has_fin;           // Whether this stream frame carries the FIN bit

    // Backwards-compat helper: high-water mark of the contributed bytes.
    uint64_t MaxOffset() const { return offset_start + length; }

    StreamDataInfo():
        stream_id(0),
        offset_start(0),
        length(0),
        has_fin(false) {}
    StreamDataInfo(uint64_t sid, uint64_t off_start, uint64_t len, bool fin):
        stream_id(sid),
        offset_start(off_start),
        length(len),
        has_fin(fin) {}
};

// Callback type for notifying stream data ACK.
// Carries the precise byte range so SendStream can do selective tracking.
using StreamDataAckCallback =
    std::function<void(uint64_t stream_id, uint64_t offset_start, uint64_t length, bool has_fin)>;

// controller of sender.
class SendControl {
public:
    SendControl(std::shared_ptr<common::ITimerScheduler> scheduler);
    ~SendControl() {
        // Every timer we own is held by a Timer handle, and ~Timer() cancels, so
        // the per-packet handles in unacked_packets_ and pto_timer_ are cleaned
        // up by their own destructors. Keeping the explicit teardown anyway:
        // ClearRetransmissionData() also settles congestion-control accounting,
        // and doing it here documents the ordering requirement.
        //
        // History: the per-packet retransmit callbacks capture `this`, and before
        // the P3 work they almost always self-completed inside the ~775 ms initial
        // PTO window, which hid the fact that nothing cancelled them. Benchmarks
        // with aggressive initial-RTT overrides (docs/internal/perf_e2e_analysis.md
        // §6 P3) shortened that window to ~100 ms and turned MultiStream teardown
        // into heap corruption. Handles make that class of bug structural rather
        // than a matter of remembering to clean up.
        pto_timer_.Cancel();
        ClearRetransmissionData();
        // Clear callbacks to prevent dangling references
        stream_data_ack_cb_ = nullptr;
        packet_lost_cb_ = nullptr;
    }

    // Bind the owning connection's lifetime guard. The connection calls this
    // once it is safely owned by a shared_ptr (e.g. on first OnPacketSend), so
    // the PTO / retransmit timers can keep the connection alive during their
    // callbacks and be skipped once the connection is destroyed. The previous
    // dummy shared_ptr<int> guard never expired, allowing the callbacks to run
    // into a half-destructed connection (TSan vptr race).
    void Bind(std::weak_ptr<void> self) {
        conn_self_ = self;
        life_token_ = self;
    }

    uint32_t GetRtt() { return rtt_calculator_.GetSmoothedRtt(); }
    uint32_t GetPTO(uint32_t max_ack_delay) { return rtt_calculator_.GetPT0Interval(max_ack_delay); }
    RttCalculator& GetRttCalculator() { return rtt_calculator_; }

    /**
     * @brief Reset the congestion controller to its initial window.
     *
     * RFC 9000 §9.4 path migration: old-path capacity estimates must not be
     * carried onto the new path. Called via SendManager::ResetPathSignals()
     * when a probe-validated new peer address becomes the active path.
     */
    void ResetCongestionControl() {
        if (congestion_control_) {
            congestion_control_->Reset();
        }
    }

    /**
     * @brief Reset the RTT estimator for a new path (RFC 9000 §9.4).
     *
     * Old-path SRTT/variance would keep driving PTO and loss detection with
     * values that may be entirely wrong on the new path. Negotiated
     * max_ack_delay / ack_delay_exponent are NOT touched (unlike
     * UpdateConfig, which would overwrite them).
     */
    void ResetRtt() { rtt_calculator_.Reset(); }
    // For test instrumentation only: returns the underlying CC's
    // bytes_in_flight / cwnd. Lets unit tests verify that send_control's
    // packet-tracking maintains exact contract with the CC layer (see
    // send_control_test.cpp G2 group).
    uint64_t GetCcBytesInFlightForTest() const { return congestion_control_->GetBytesInFlight(); }
    uint64_t GetCcCongestionWindowForTest() const { return congestion_control_->GetCongestionWindow(); }
    void OnPacketSend(uint64_t now, const std::shared_ptr<IPacket>& packet, uint32_t pkt_len);
    void OnPacketSend(uint64_t now, const std::shared_ptr<IPacket>& packet, uint32_t pkt_len,
        const std::vector<StreamDataInfo>& stream_data);
    void OnPacketAck(uint64_t now, PacketNumberSpace ns, const std::shared_ptr<IFrame>& ack_frame);
    void CanSend(uint64_t now, uint64_t& can_send_bytes);
    bool NeedReSend() { return !lost_packets_.empty(); }

    // A lost packet plus the stream-data records originally attached to it.
    // The retransmit path (BaseConnection::TrySend) needs the stream_data
    // when re-registering the packet under its new PN, otherwise the
    // re-encoded packet's eventual ACK would not propagate back to
    // SendStream and the stream's selective-ACK bookkeeping would be left
    // with permanent gaps — exactly the failure mode that left aioquic
    // transfer interop hanging on 5MB downloads.
    struct LostPacketEntry {
        std::shared_ptr<IPacket> packet;
        std::vector<StreamDataInfo> stream_data;
    };
    std::list<LostPacketEntry>& GetLostPacket() { return lost_packets_; }
    uint64_t GetNextSendTime(uint64_t now) { return congestion_control_->NextSendTime(now); }

    void UpdateConfig(const TransportParam& tp);

    // Set callback for stream data ACK notification
    void SetStreamDataAckCallback(StreamDataAckCallback callback) { stream_data_ack_cb_ = callback; }

    // Set callback for packet loss notification
    using PacketLostCallback = std::function<void(std::shared_ptr<IPacket>)>;
    void SetPacketLostCallback(PacketLostCallback callback) { packet_lost_cb_ = callback; }

    // Set callback for handshake probe needed (RFC 9002 §6.2.2.1)
    // Called when PTO fires during handshake but no ACK-eliciting data to retransmit
    using ProbeNeededCallback = std::function<void()>;
    void SetProbeNeededCallback(ProbeNeededCallback callback) { probe_needed_cb_ = callback; }

    // RFC 9002 §6.2.4 (post-handshake PTO probe):
    // Called when the PTO timer fires AFTER the handshake is complete, in
    // addition to (not instead of) retransmitting the oldest unacked packet.
    // The connection layer should respond by emitting an ack-eliciting packet
    // (typically a PING) so the peer is forced to send an ACK that advances
    // packet-threshold loss detection. Without this, a flow-controlled sender
    // whose only outstanding bytes are retransmissions of already-retransmitted
    // packets has no way to coax the peer into an ACK and the connection
    // stalls until idle-timeout (Bug-19 / FC-locked PTO probe gap, see
    // docs/diagnosis/transfer_5mb_stall.md).
    using ApplicationProbeCallback = std::function<void()>;
    void SetApplicationProbeCallback(ApplicationProbeCallback callback) { application_probe_cb_ = callback; }

    // Mark handshake as complete (disables handshake probe timer)
    void SetHandshakeComplete() { handshake_complete_ = true; }

    // RFC 9002 §6.2.1: While the handshake is unconfirmed, the peer's
    // max_ack_delay transport parameter has not yet been reliably delivered,
    // so it MUST be treated as zero when computing PTO. All four PTO
    // callsites in send_control.cpp (OnPacketSend's per-packet timer,
    // OnPacketSend's pto_timer_ rearm, OnPacketAck's handshake-phase rearm,
    // and OnPTOTimer's post-fire rearm) route through this accessor so the
    // pre-handshake PTO stays spec-compliant. Returns 0 pre-handshake-complete,
    // max_ack_delay_ afterwards.
    uint32_t GetEffectiveMaxAckDelay() const { return handshake_complete_ ? max_ack_delay_ : 0u; }

    // Clear all retransmission data (used when connection is closing)
    void ClearRetransmissionData();

    // Set qlog trace for instrumentation
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace);

    // qlog draft-03 packet/datagram coalescing instrumentation.
    //
    // The connection layer calls BeginSendDatagram(id) at the start of
    // building one UDP datagram (which may carry one or several coalesced
    // QUIC packets). Every subsequent OnPacketSend until the next
    // BeginSendDatagram (or until EndSendDatagram is called) tags its
    // PacketSentData with that id and is accumulated into the per-datagram
    // counters. EndSendDatagram() returns those counters so the caller can
    // emit a transport:datagrams_sent event, then the counters reset to 0.
    //
    // Both calls are cheap no-ops when no qlog trace is installed.
    void BeginSendDatagram(uint64_t datagram_id);
    struct SendDatagramSummary {
        uint64_t datagram_id = 0;
        uint32_t packet_count = 0;
        uint32_t raw_length = 0;
        std::vector<uint64_t> packet_numbers;
    };
    SendDatagramSummary EndSendDatagram();

    // RFC 9000 Section 4.10: Discard packet number space state
    void DiscardPacketNumberSpace(PacketNumberSpace ns);

    // Reset Initial packet number to 0 (used for Retry)
    void ResetInitialPacketNumber();

private:
    // RFC 9002 Section 6.1.1: Loss detection constants
    static constexpr uint32_t kPacketThreshold = 3;   // Packets before declaring loss
    static constexpr uint32_t kTimeThresholdNum = 9;  // Time threshold = 9/8 * RTT
    static constexpr uint32_t kTimeThresholdDen = 8;

    // RFC 9002 Section 6.1: Detect lost packets based on packet/time threshold
    void DetectLostPackets(uint64_t now, PacketNumberSpace ns, uint64_t largest_acked);
    enum class EcnState { kUnknown, kValidated, kFailed };
    std::list<LostPacketEntry> lost_packets_;
    struct PacketTimerInfo {
        uint64_t send_time_;
        uint32_t pkt_len_;
        // Retransmit timer for this packet. Move-only, which is exactly the
        // point: the previous `common::TimerTask` was a *copy* of the
        // registration, and cancelling through a copy only worked because the
        // wheel looked the id up in a side table and shrugged when it was
        // missing. With a handle there is one owner, here, and losing it cancels.
        common::Timer timer_;
        std::vector<StreamDataInfo> stream_data;  // Stream data contained in this packet
        std::shared_ptr<IPacket> packet;          // Store packet for retransmission
        bool is_lost = false;

        PacketTimerInfo() {}
        PacketTimerInfo(uint64_t t, uint32_t len, common::Timer&& timer):
            send_time_(t),
            pkt_len_(len),
            timer_(std::move(timer)) {}
        PacketTimerInfo(uint64_t t, uint32_t len, common::Timer&& timer, const std::vector<StreamDataInfo>& data):
            send_time_(t),
            pkt_len_(len),
            timer_(std::move(timer)),
            stream_data(data) {}
        PacketTimerInfo(uint64_t t, uint32_t len, common::Timer&& timer, const std::vector<StreamDataInfo>& data,
            std::shared_ptr<IPacket> pkt):
            send_time_(t),
            pkt_len_(len),
            timer_(std::move(timer)),
            stream_data(data),
            packet(pkt) {}
    };
    std::unordered_map<uint64_t, PacketTimerInfo> unacked_packets_[PacketNumberSpace::kNumberSpaceCount];

    StreamDataAckCallback stream_data_ack_cb_;
    PacketLostCallback packet_lost_cb_;
    ProbeNeededCallback probe_needed_cb_;
    ApplicationProbeCallback application_probe_cb_;
    bool handshake_complete_ = false;

    uint64_t pkt_num_largest_sent_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t pkt_num_largest_acked_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t largest_sent_time_[PacketNumberSpace::kNumberSpaceCount] = {0};

    // ECN validation state per packet number space
    uint64_t prev_ect0_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t prev_ect1_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t prev_ce_[PacketNumberSpace::kNumberSpaceCount] = {0};
    EcnState ecn_state_[PacketNumberSpace::kNumberSpaceCount] = {EcnState::kUnknown};

    RttCalculator rtt_calculator_;
    std::unique_ptr<ICongestionControl> congestion_control_;

    uint32_t max_ack_delay_ = 0;
    uint32_t ack_delay_exponent_ = 0;
    std::shared_ptr<common::ITimerScheduler> scheduler_;
    // Guards every callback registered from here. It is a weak_ptr to the owning
    // connection: EventLoop::FireSlot skips the callback once the connection is
    // destroyed (see if_timer_scheduler.h). The previous dummy shared_ptr<int>
    // never expired, so the guard was inert and callbacks could run into a
    // half-destructed connection (TSan vptr race). All our callbacks capture a
    // raw `this`, so the connection must stay alive for their duration.
    std::weak_ptr<void> life_token_;
    std::weak_ptr<void> conn_self_;

    // RFC 9002: PTO timer for detecting persistent timeouts
    common::Timer pto_timer_;
    uint64_t last_ack_eliciting_sent_time_ = 0;  // Track when we last sent ack-eliciting data

    // RFC 9002: PTO timer callback
    void OnPTOTimer();

    // Arm or rearm the PTO timer. Rearm is the common case (once per outgoing
    // packet), and it reuses the existing timer node.
    void ArmPtoTimer(uint64_t delay_ms);

    // Qlog trace for instrumentation
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    // qlog draft-03 datagram coalescing accumulator (see BeginSendDatagram /
    // EndSendDatagram). Reset to id=0 / count=0 when no datagram is in
    // progress so accidental reads still produce a benign zero.
    uint64_t current_send_datagram_id_ = 0;
    uint32_t current_send_packet_count_ = 0;
    uint32_t current_send_raw_length_ = 0;
    std::vector<uint64_t> current_send_packet_numbers_;

    // Helper for logging recovery metrics with sampling
    void LogRecoveryMetricsIfChanged(uint64_t now);

    // Sampling state for recovery_metrics_updated
    uint64_t last_logged_cwnd_ = 0;
    uint64_t last_metrics_log_time_ = 0;
};

}  // namespace quic
}  // namespace quicx

#endif