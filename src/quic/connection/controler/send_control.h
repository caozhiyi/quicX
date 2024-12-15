#ifndef QUIC_CONNECTION_CONTROLER_SEND_CONTROL
#define QUIC_CONNECTION_CONTROLER_SEND_CONTROL

#include <list>
#include <unordered_map>
#include "quic/packet/type.h"
#include "common/timer/timer_task.h"
#include "common/timer/if_timer.h"
#include "quic/packet/if_packet.h"
#include "quic/connection/controler/rtt_calculator.h"

namespace quicx {
namespace quic {

// controller of sender. 
class SendControl {
public:
    SendControl(std::shared_ptr<common::ITimer> timer);
    ~SendControl() {}

    void OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet);
    void OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> ack_frame);
    bool NeedReSend() { return !lost_packets_.empty(); }
    std::list<std::shared_ptr<IPacket>>& GetLostPacket() { return lost_packets_; }

private:
    std::list<std::shared_ptr<IPacket>> lost_packets_;
    struct PacketTimerInfo {
        uint64_t send_time_;
        common::TimerTask timer_task_;
        PacketTimerInfo() {}
        PacketTimerInfo(uint64_t t, const common::TimerTask& task): send_time_(t), timer_task_(task) {}
    };
    std::unordered_map<uint64_t, PacketTimerInfo> unacked_packets_[PNS_NUMBER];

    uint64_t pkt_num_largest_sent_[PNS_NUMBER];
    uint64_t pkt_num_largest_acked_[PNS_NUMBER];
    uint64_t largest_sent_time_[PNS_NUMBER];

    RttCalculator rtt_calculator_;
    std::shared_ptr<common::ITimer> timer_;
};

}
}

#endif