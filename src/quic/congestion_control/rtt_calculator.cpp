#include <limits>
#include <cstdint>
#include <algorithm>
#include "common/log/log.h"
#include "quic/congestion_control/rtt_calculator.h"

namespace quicx {

RttCalculator::RttCalculator() {
    Reset();
}

RttCalculator::~RttCalculator() {

}

bool RttCalculator::UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay) {
    LOG_DEBUG("update rtt. send time:%lld, now:%lld, ack delay:%d", send_time, now, ack_delay);

    _latest_rtt = now - send_time;
    // first update rtt
    if (_last_update_time == 0) {
        _min_rtt = _latest_rtt;
        _smoothed_rtt = _latest_rtt;
        _rtt_var = _latest_rtt >> 1;
    
    } else {
        _min_rtt = std::min(_min_rtt, _latest_rtt);

        // TODO 应该（SHOULD）忽略对端的max_ack_delay直到握手确认
        // 必须（MUST）使用确认延迟和握手确认后对端的max_ack_delay中较小的一个

        uint32_t adjusted_rtt = _latest_rtt;
        if (_latest_rtt >= (_min_rtt + ack_delay)) {
            adjusted_rtt -= ack_delay;
        }

        // smoothed_rtt = 7/8 * smoothed_rtt + 1/8 * adjusted_rtt
        _smoothed_rtt = _smoothed_rtt - _smoothed_rtt >> 3 + adjusted_rtt >> 3;

        // rttvar_sample = abs(smoothed_rtt - adjusted_rtt)
        uint32_t rttvar_sample = _smoothed_rtt > adjusted_rtt ? _smoothed_rtt - adjusted_rtt : adjusted_rtt - _smoothed_rtt;

        // rttvar = 3/4 * rttvar + 1/4 * rttvar_sample
        _rtt_var = _rtt_var - _rtt_var >> 2 + rttvar_sample >> 2;
    }
    _last_update_time = now;
}

void RttCalculator::Reset() {
    _latest_rtt = 0;
    _smoothed_rtt = __init_rtt * 1000;
    _rtt_var = _smoothed_rtt / 2;
    _min_rtt = std::numeric_limits<uint32_t>::max();

    _last_update_time = 0;
}

uint32_t RttCalculator::GetPT0Interval(uint32_t max_ack_delay) {
    // PTO = smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay
    return _smoothed_rtt + std::max<uint32_t>(_rtt_var << 2, 1000) + max_ack_delay;
}

}
