#ifndef QUIC_CONNECTION_CONTROLER_SEND_CONTROL
#define QUIC_CONNECTION_CONTROLER_SEND_CONTROL

#include <list>
#include <unordered_map>
#include "quic/packet/type.h"
#include "common/timer/timer_task.h"
#include "common/timer/timer_interface.h"
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
    bool NeedReSend() { return !_lost_packets.empty(); }
    std::list<std::shared_ptr<IPacket>>& GetLostPacket() { return _lost_packets; }

private:
    std::list<std::shared_ptr<IPacket>> _lost_packets;
    struct PacketTimerInfo {
        uint64_t _send_time;
        common::TimerTask _timer_task;
        PacketTimerInfo() {}
        PacketTimerInfo(uint64_t t, const common::TimerTask& task): _send_time(t), _timer_task(task) {}
    };
    std::unordered_map<uint64_t, PacketTimerInfo> _unacked_packets[PNS_NUMBER];

    uint64_t _pkt_num_largest_sent[PNS_NUMBER];
    uint64_t _pkt_num_largest_acked[PNS_NUMBER];
    uint64_t _largest_sent_time[PNS_NUMBER];

    RttCalculator _rtt_calculator;
    std::shared_ptr<common::ITimer> _timer;
};

}
}

#endif