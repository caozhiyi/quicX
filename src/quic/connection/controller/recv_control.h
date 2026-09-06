#ifndef QUIC_CONNECTION_CONTROLER_RECV_CONTROL
#define QUIC_CONNECTION_CONTROLER_RECV_CONTROL

#include <functional>
#include <memory>
#include <set>

#include "common/timer/if_timer_scheduler.h"

#include "quic/config.h"
#include "quic/connection/transport_param.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

// controller of receiver.
/*
1. An ACK must be sent within the max_ack_delay time, so make a timer.
2. Immediately acknowledge all Initial and Handshake trigger packets, and acknowledge all 0-RTT and 1-RTT trigger
packets within the announced max_ack_delay, except in the following cases: before handshake confirmation, the endpoint
may not have the keys available to decrypt Handshake, 0-RTT, or 1-RTT packets upon receipt.
3. MUST NOT send non-ACK trigger packets in response to non-ACK trigger packets.
4. To help the sender with loss detection, the endpoint SHOULD generate and send an ACK frame immediately upon receiving
an ACK trigger packet: When the packet number of the received packet is less than another received ACK trigger packet;
   When the packet number of the received packet is greater than the highest packet number of any received ACK trigger
packet, and the packet numbers are not contiguous.
5. The receiver SHOULD send an ACK frame only after receiving at least two ACK trigger packets.
6. The receiver SHOULD include an ACK Range in each ACK frame, which contains the largest received packet number.
*/
class RecvControl {
public:
    RecvControl(std::shared_ptr<common::ITimerScheduler> scheduler);
    ~RecvControl() {
        // ~Timer() cancels, so the delayed-ACK timer cannot fire into a
        // destroyed RecvControl (its callback captures a raw `this`).
        ack_delay_timer_.Cancel();
        // Clear callbacks to prevent dangling references
        immediate_ack_cb_ = nullptr;
        active_send_cb_ = nullptr;
    }

    // Bind the owning connection's lifetime guard. The connection calls this
    // once it is safely owned by a shared_ptr (e.g. on first OnPacketRecv), so
    // the delayed-ACK timer can keep the connection alive during its callback
    // and be skipped once the connection is destroyed. Without this the timer
    // would fire into a half-destructed connection (TSan vptr race).
    void Bind(std::weak_ptr<void> self) {
        conn_self_ = self;
        life_token_ = self;
    }

    void OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet);
    void OnEcnCounters(uint8_t ecn, PacketNumberSpace ns);
    std::shared_ptr<IFrame> MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns, bool ecn_enabled = true);

    // Check if there are packets waiting to be ACKed
    bool HasPendingAck(PacketNumberSpace ns) const { return !wait_ack_packet_numbers_[ns].empty(); }

    // Check whether an ACK frame should be emitted *right now* for `ns`.
    //
    // PERF FIX (P0): The previous send path treated any non-empty
    // `wait_ack_packet_numbers_` as a reason to attach an ACK frame to the
    // very next outgoing packet. On fast paths that effectively defeated
    // both `kAckThreshold` (RFC 9000 §13.2.2) and `max_ack_delay_` — a
    // single inbound packet would be ACKed immediately by the next
    // app-level TrySend(), giving a 1:1 packet/ACK ratio and starving
    // throughput.
    //
    // The new contract: an ACK is "due now" only when one of the
    // explicit triggers fired:
    //   - Initial / Handshake space (RFC 9000 §13.2.1: MUST ACK immediately).
    //   - `ack_immediately_pending_` was set by ShouldSendImmediateAck()
    //     (threshold / OoO / gap / ECN-CE).
    //   - The max_ack_delay_ timer expired (active_send_cb_ marks the
    //     space as due before re-running the send loop).
    // Otherwise the ACK keeps aggregating in `wait_ack_packet_numbers_`.
    bool ShouldSendAckNow(PacketNumberSpace ns) const;

    // Get largest received packet number for a given packet number space
    // Used for PN recovery per RFC 9000 Appendix A
    uint64_t GetLargestReceivedPn(PacketNumberSpace ns) const { return pkt_num_largest_recvd_[ns]; }

    // RFC 9000 Section 4.10: Discard packet number space when keys discarded
    void DiscardPacketNumberSpace(PacketNumberSpace ns);

    // Callback for immediate ACK (with packet number space parameter)
    void SetImmediateAckCB(std::function<void(PacketNumberSpace)> cb) { immediate_ack_cb_ = cb; }

    // Callback for delayed ACK (used for Application packets)
    void SetActiveSendCB(std::function<void()> cb) { active_send_cb_ = cb; }
    void UpdateConfig(const TransportParam& tp);

private:
    // RFC 9000 Section 13.2.1: Determine if immediate ACK is required
    bool ShouldSendImmediateAck(PacketNumberSpace ns, uint64_t pkt_num, uint8_t ecn);

    uint64_t pkt_num_largest_recvd_[PacketNumberSpace::kNumberSpaceCount];
    uint64_t largest_recv_time_[PacketNumberSpace::kNumberSpaceCount];
    std::set<uint64_t> wait_ack_packet_numbers_[PacketNumberSpace::kNumberSpaceCount];
    // ACK-ELICITING subset of wait_ack_packet_numbers_ — these are the only
    // packets that carry an ACK obligation (RFC 9000 §13.2). ACK-only packets
    // are reported in ACK ranges but never make a space "ack due"; keying the
    // Initial/Handshake MUST-ACK path on the combined set caused an unbounded
    // ACK-of-ACK ping-pong between two quicx endpoints (2026-09-01).
    std::set<uint64_t> wait_ack_eliciting_[PacketNumberSpace::kNumberSpaceCount];
    // Bounded ACK-of-ACK budget per PN space (see OnPacketRecv): replies to
    // ACK-only packets in Initial/Handshake are the sole recovery channel for
    // peers that only send ACK-only handshake packets after losing their
    // Finished (quinn), but must be capped so two quicx endpoints don't ACK
    // each other's ACK-only packets forever. Reset by any ack-eliciting
    // packet in the same space.
    uint32_t ack_only_replies_[PacketNumberSpace::kNumberSpaceCount]{0, 0, 0};
    // RFC 9000 §13.2.1 para 6: "an endpoint SHOULD acknowledge at least
    // every second ACK-only packet" it receives. Consecutive ACK-only
    // packets in the Application space are the peer's only loss-detection
    // clock when our own data/response datagram was lost: peers whose PTO
    // sends probes without retransmitting stream data (observed: aioquic
    // corruption runs, 2026-09-03) rely on our ACK ranges (the gap at the
    // lost packet) to declare it lost and retransmit. Keyed on *consecutive*
    // ACK-only packets; any ack-eliciting packet resets the counter, so a
    // quicx<->quicx exchange self-terminates instead of ping-ponging.
    uint32_t ack_only_seq_[PacketNumberSpace::kNumberSpaceCount]{0, 0, 0};
    // ECN counters per PN space
    uint64_t ect0_count_[PacketNumberSpace::kNumberSpaceCount]{0};
    uint64_t ect1_count_[PacketNumberSpace::kNumberSpaceCount]{0};
    uint64_t ce_count_[PacketNumberSpace::kNumberSpaceCount]{0};

    bool set_timer_;
    // PERF FIX (P0): A space is "ack-due" iff it has packets waiting AND one
    // of the immediate-ACK triggers fired (RFC 9000 §13.2.1/§13.2.2) or the
    // max_ack_delay_ timer expired. Set by OnPacketRecv (when
    // ShouldSendImmediateAck() returns true) and by the timer task; cleared
    // inside MayGenerateAckFrame() once the ACK is emitted.
    bool ack_due_[PacketNumberSpace::kNumberSpaceCount]{false, false, false};
    std::shared_ptr<common::ITimerScheduler> scheduler_;
    // Guards the delayed-ACK callback. It is a weak_ptr to the owning
    // connection: EventLoop::FireSlot skips the callback once the connection is
    // destroyed (see if_timer_scheduler.h). Using a dummy shared_ptr<int> here
    // (the previous behaviour) made the guard never expire, allowing the
    // callback to run into a half-destructed connection (TSan vptr race).
    std::weak_ptr<void> life_token_;
    std::weak_ptr<void> conn_self_;
    common::Timer ack_delay_timer_;
    void ArmAckDelayTimer();
    std::function<void(PacketNumberSpace)> immediate_ack_cb_;  // Immediate ACK callback
    std::function<void()> active_send_cb_;                     // Delayed ACK callback

    uint32_t max_ack_delay_;
    uint32_t ack_delay_exponent_{3};

    // Metrics: ACK frequency tracking
    uint64_t ack_count_{0};
    uint64_t last_ack_time_{0};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_RECV_CONTROL