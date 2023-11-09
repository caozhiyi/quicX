#include <cstring>
#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/controler/send_control.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {

SendControl::SendControl(std::shared_ptr<ITimer> timer): _timer(timer) {
    memset(_pkt_num_largest_sent, 0, sizeof(_pkt_num_largest_sent));
    memset(_pkt_num_largest_acked, 0, sizeof(_pkt_num_largest_acked));
    memset(_largest_sent_time, 0, sizeof(_largest_sent_time));
}

void SendControl::OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet) {
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    if (_pkt_num_largest_sent[ns] >= packet->GetPacketNumber()) {
        LOG_ERROR("invalid packet number. number:%d", packet->GetPacketNumber());
        return;
    }
    _pkt_num_largest_sent[ns] = packet->GetPacketNumber();
    _largest_sent_time[ns] = UTCTimeMsec();

    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }
    auto timer_task = TimerTask([this, packet]{
        _lost_packets.push_back(packet);
    });
    _timer->AddTimer(timer_task, _rtt_calculator.GetPT0Interval(TransportParamConfig::Instance()._max_ack_delay));
    _unacked_packets[ns][packet->GetPacketNumber()] = PacketTimerInfo(_largest_sent_time[ns], timer_task);
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    if (frame->GetType() != FT_ACK) {
        LOG_ERROR("invalid frame on packet ack.");
        return;
    }

    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    uint64_t pkt_num = ack_frame->GetLargestAck();
    if (_pkt_num_largest_acked[ns] < pkt_num) {
        _pkt_num_largest_acked[ns] = pkt_num;

        auto iter = _unacked_packets[ns].find(pkt_num);
        if (iter != _unacked_packets[ns].end()) {
            _rtt_calculator.UpdateRtt(iter->second._send_time, now, ack_frame->GetAckDelay());
        }
    }

    for (uint32_t i = 0; i <= ack_frame->GetFirstAckRange(); i++) {
        auto task = _unacked_packets[ns].find(pkt_num--);
        if (task != _unacked_packets[ns].end()) {
            _timer->RmTimer(task->second._timer_task);
        }
    }

    auto ranges = ack_frame->GetAckRange();
    for (auto iter = ranges.begin(); iter != ranges.end(); iter++) {
        pkt_num = pkt_num - iter->GetGap();
        for (uint32_t i = 0; i <= iter->GetAckRangeLength(); i++) {
            auto task = _unacked_packets[ns].find(pkt_num--);
            if (task != _unacked_packets[ns].end()) {
                _timer->RmTimer(task->second._timer_task);
            }
        }
    }
}

}