#ifndef QUIC_CONNECTION_CONTROLER_RECV_CONTROL
#define QUIC_CONNECTION_CONTROLER_RECV_CONTROL

#include <functional>
#include <set>

#include "common/timer/if_timer.h"

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
    RecvControl(std::shared_ptr<common::ITimer> timer);
    ~RecvControl() {
        // Cancel timer to prevent use-after-free when timer fires after destruction
        if (timer_ && set_timer_) {
            timer_->RemoveTimer(timer_task_);
        }
        // Clear callbacks to prevent dangling references
        immediate_ack_cb_ = nullptr;
        active_send_cb_ = nullptr;
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
    std::shared_ptr<common::ITimer> timer_;
    common::TimerTask timer_task_;
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

#endif