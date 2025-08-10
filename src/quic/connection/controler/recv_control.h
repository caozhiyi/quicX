#ifndef QUIC_CONNECTION_CONTROLER_RECV_CONTROL
#define QUIC_CONNECTION_CONTROLER_RECV_CONTROL

#include <set>
#include <functional>
#include "quic/packet/type.h"
#include "common/timer/if_timer.h"
#include "quic/packet/if_packet.h"
#include "quic/connection/transport_param.h"
namespace quicx {
namespace quic {

// controller of receiver. 
/*
1. An ACK must be sent within the max_ack_delay time, so make a timer.
2. Immediately acknowledge all Initial and Handshake trigger packets, and acknowledge all 0-RTT and 1-RTT trigger packets within the announced max_ack_delay, except in the following cases: before handshake confirmation, the endpoint may not have the keys available to decrypt Handshake, 0-RTT, or 1-RTT packets upon receipt.
3. MUST NOT send non-ACK trigger packets in response to non-ACK trigger packets.
4. To help the sender with loss detection, the endpoint SHOULD generate and send an ACK frame immediately upon receiving an ACK trigger packet:
   When the packet number of the received packet is less than another received ACK trigger packet;
   When the packet number of the received packet is greater than the highest packet number of any received ACK trigger packet, and the packet numbers are not contiguous.
5. The receiver SHOULD send an ACK frame only after receiving at least two ACK trigger packets.
6. The receiver SHOULD include an ACK Range in each ACK frame, which contains the largest received packet number.
*/
class RecvControl {
public:
    RecvControl(std::shared_ptr<common::ITimer> timer);
    ~RecvControl() {}

    void OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet);
    void OnEcnCounters(uint8_t ecn, PacketNumberSpace ns);
    std::shared_ptr<IFrame> MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns, bool ecn_enabled = true);

    void SetActiveSendCB(std::function<void()> cb) { active_send_cb_ = cb; }
    void UpdateConfig(const TransportParam& tp);

private:
    uint64_t pkt_num_largest_recvd_[PacketNumberSpace::kNumberSpaceCount];
    uint64_t largest_recv_time_[PacketNumberSpace::kNumberSpaceCount];
    std::set<uint64_t> wait_ack_packet_numbers_[PacketNumberSpace::kNumberSpaceCount];
    // ECN counters per PN space
    uint64_t ect0_count_[PacketNumberSpace::kNumberSpaceCount] {0};
    uint64_t ect1_count_[PacketNumberSpace::kNumberSpaceCount] {0};
    uint64_t ce_count_[PacketNumberSpace::kNumberSpaceCount] {0};
    
    bool set_timer_;
    std::shared_ptr<common::ITimer> timer_;

    common::TimerTask timer_task_;
    std::function<void()> active_send_cb_;

    uint32_t max_ack_delay_;
};

}
}

#endif