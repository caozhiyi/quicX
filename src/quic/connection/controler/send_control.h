#ifndef QUIC_CONNECTION_CONTROLER_SEND_CONTROL
#define QUIC_CONNECTION_CONTROLER_SEND_CONTROL

#include <functional>
#include <list>
#include <unordered_map>
#include <vector>

#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/connection/controler/rtt_calculator.h"
#include "quic/connection/transport_param.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

// Stream data info in a packet for ACK tracking
struct StreamDataInfo {
    uint64_t stream_id;
    uint64_t max_offset;  // Maximum offset of this stream in this packet
    bool has_fin;         // Whether this packet contains FIN for this stream

    StreamDataInfo():
        stream_id(0),
        max_offset(0),
        has_fin(false) {}
    StreamDataInfo(uint64_t sid, uint64_t offset, bool fin):
        stream_id(sid),
        max_offset(offset),
        has_fin(fin) {}
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
    RttCalculator& GetRttCalculator() { return rtt_calculator_; }
    void OnPacketSend(uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len);
    void OnPacketSend(uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len,
        const std::vector<StreamDataInfo>& stream_data);
    void OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> ack_frame);
    void CanSend(uint64_t now, uint64_t& can_send_bytes);
    bool NeedReSend() { return !lost_packets_.empty(); }
    std::list<std::shared_ptr<IPacket>>& GetLostPacket() { return lost_packets_; }
    uint64_t GetNextSendTime(uint64_t now) { return congestion_control_->NextSendTime(now); }

    void UpdateConfig(const TransportParam& tp);

    // Set callback for stream data ACK notification
    void SetStreamDataAckCallback(StreamDataAckCallback callback) { stream_data_ack_cb_ = callback; }

    // Set callback for packet loss notification
    using PacketLostCallback = std::function<void(std::shared_ptr<IPacket>)>;
    void SetPacketLostCallback(PacketLostCallback callback) { packet_lost_cb_ = callback; }

    // Clear all retransmission data (used when connection is closing)
    void ClearRetransmissionData();

    // RFC 9000 Section 4.10: Discard packet number space state
    void DiscardPacketNumberSpace(PacketNumberSpace ns);

private:
    // RFC 9002 Section 6.1.1: Loss detection constants
    static constexpr uint32_t kPacketThreshold = 3;   // Packets before declaring loss
    static constexpr uint32_t kTimeThresholdNum = 9;  // Time threshold = 9/8 * RTT
    static constexpr uint32_t kTimeThresholdDen = 8;

    // RFC 9002 Section 6.1: Detect lost packets based on packet/time threshold
    void DetectLostPackets(uint64_t now, PacketNumberSpace ns, uint64_t largest_acked);
    enum class EcnState { kUnknown, kValidated, kFailed };
    std::list<std::shared_ptr<IPacket>> lost_packets_;
    struct PacketTimerInfo {
        uint64_t send_time_;
        uint32_t pkt_len_;
        common::TimerTask timer_task_;
        std::vector<StreamDataInfo> stream_data;  // Stream data contained in this packet
        std::shared_ptr<IPacket> packet;          // Store packet for retransmission
        bool is_lost = false;

        PacketTimerInfo() {}
        PacketTimerInfo(uint64_t t, uint32_t len, const common::TimerTask& task):
            send_time_(t),
            pkt_len_(len),
            timer_task_(task) {}
        PacketTimerInfo(
            uint64_t t, uint32_t len, const common::TimerTask& task, const std::vector<StreamDataInfo>& data):
            send_time_(t),
            pkt_len_(len),
            timer_task_(task),
            stream_data(data) {}
        PacketTimerInfo(uint64_t t, uint32_t len, const common::TimerTask& task,
            const std::vector<StreamDataInfo>& data, std::shared_ptr<IPacket> pkt):
            send_time_(t),
            pkt_len_(len),
            timer_task_(task),
            stream_data(data),
            packet(pkt) {}
    };
    std::unordered_map<uint64_t, PacketTimerInfo> unacked_packets_[PacketNumberSpace::kNumberSpaceCount];

    StreamDataAckCallback stream_data_ack_cb_;
    PacketLostCallback packet_lost_cb_;

    uint64_t pkt_num_largest_sent_[PacketNumberSpace::kNumberSpaceCount];
    uint64_t pkt_num_largest_acked_[PacketNumberSpace::kNumberSpaceCount];
    uint64_t largest_sent_time_[PacketNumberSpace::kNumberSpaceCount];

    // ECN validation state per packet number space
    uint64_t prev_ect0_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t prev_ect1_[PacketNumberSpace::kNumberSpaceCount] = {0};
    uint64_t prev_ce_[PacketNumberSpace::kNumberSpaceCount] = {0};
    EcnState ecn_state_[PacketNumberSpace::kNumberSpaceCount] = {EcnState::kUnknown};

    RttCalculator rtt_calculator_;
    std::unique_ptr<ICongestionControl> congestion_control_;

    uint32_t max_ack_delay_;
    uint32_t ack_delay_exponent_;
    std::shared_ptr<common::ITimer> timer_;

    // RFC 9002: PTO timer for detecting persistent timeouts
    common::TimerTask pto_timer_;
    uint64_t last_ack_eliciting_sent_time_ = 0;  // Track when we last sent ack-eliciting data

    // RFC 9002: PTO timer callback
    void OnPTOTimer();
};

}  // namespace quic
}  // namespace quicx

#endif