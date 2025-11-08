#ifndef QUIC_CONNECTION_CONTROLER_SEND_CONTROL
#define QUIC_CONNECTION_CONTROLER_SEND_CONTROL

#include <list>
#include <vector>
#include <functional>
#include <unordered_map>

#include "quic/packet/type.h"
#include "common/timer/if_timer.h"
#include "quic/packet/if_packet.h"
#include "common/timer/timer_task.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/controler/rtt_calculator.h"
#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

// Stream data info in a packet for ACK tracking
struct StreamDataInfo {
    uint64_t stream_id;
    uint64_t max_offset;  // Maximum offset of this stream in this packet
    bool has_fin;         // Whether this packet contains FIN for this stream
    
    StreamDataInfo() : stream_id(0), max_offset(0), has_fin(false) {}
    StreamDataInfo(uint64_t sid, uint64_t offset, bool fin) 
        : stream_id(sid), max_offset(offset), has_fin(fin) {}
};

// Callback type for notifying stream data ACK
using StreamDataAckCallback = std::function<void(uint64_t stream_id, uint64_t max_offset, bool has_fin)>;

// controller of sender. 
class SendControl {
public:
    SendControl(std::shared_ptr<common::ITimer> timer);
    ~SendControl() {}

    uint32_t GetRtt() { return rtt_calculator_.GetSmoothedRtt(); }
    uint32_t GetPTO(uint32_t max_ack_delay) { return rtt_calculator_.GetPT0Interval(max_ack_delay); }
    void OnPacketSend(uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len);
    void OnPacketSend(uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len, 
                      const std::vector<StreamDataInfo>& stream_data);
    void OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> ack_frame);
    void CanSend(uint64_t now, uint64_t& can_send_bytes);
    bool NeedReSend() { return !lost_packets_.empty(); }
    std::list<std::shared_ptr<IPacket>>& GetLostPacket() { return lost_packets_; }

    void UpdateConfig(const TransportParam& tp);
    
    // Set callback for stream data ACK notification
    void SetStreamDataAckCallback(StreamDataAckCallback callback) { 
        stream_data_ack_cb_ = callback; 
    }

private:
    enum class EcnState { kUnknown, kValidated, kFailed };
    std::list<std::shared_ptr<IPacket>> lost_packets_;
    struct PacketTimerInfo {
        uint64_t send_time_;
        uint32_t pkt_len_;
        common::TimerTask timer_task_;
        std::vector<StreamDataInfo> stream_data;  // Stream data contained in this packet
        
        PacketTimerInfo() {}
        PacketTimerInfo(uint64_t t, uint32_t len, const common::TimerTask& task): 
            send_time_(t), pkt_len_(len), timer_task_(task) {}
        PacketTimerInfo(uint64_t t, uint32_t len, const common::TimerTask& task,
                       const std::vector<StreamDataInfo>& data): 
            send_time_(t), pkt_len_(len), timer_task_(task), stream_data(data) {}
    };
    std::unordered_map<uint64_t, PacketTimerInfo> unacked_packets_[PacketNumberSpace::kNumberSpaceCount];
    
    StreamDataAckCallback stream_data_ack_cb_;

    uint64_t pkt_num_largest_sent_[PacketNumberSpace::kNumberSpaceCount];
    uint64_t pkt_num_largest_acked_[PacketNumberSpace::kNumberSpaceCount];
    uint64_t largest_sent_time_[PacketNumberSpace::kNumberSpaceCount];

    // ECN validation state per packet number space
    uint64_t prev_ect0_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t prev_ect1_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t prev_ce_[PacketNumberSpace::kNumberSpaceCount] = {0};
    EcnState ecn_state_[PacketNumberSpace::kNumberSpaceCount] = {EcnState::kUnknown};

    RttCalculator rtt_calculator_;
    std::shared_ptr<common::ITimer> timer_;
    std::unique_ptr<ICongestionControl> congestion_control_;

    uint32_t max_ack_delay_;
    uint32_t ack_delay_exponent_;
};

}
}

#endif