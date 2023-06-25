#ifndef QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR
#define QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR

#include <cstdint>

namespace quicx {

const static uint32_t __init_rtt = 250;

class RttCalculator {
public:
    RttCalculator();
    ~RttCalculator();

    bool UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay);

    void Reset();

    uint32_t GetPT0Interval(uint32_t max_ack_delay);

    uint32_t GetLatestRtt() { return _latest_rtt; }
    uint32_t GetRttVar() { return _rtt_var; }
    uint32_t GetMinRtt() { return _min_rtt; }
    uint32_t GetSmoothedRtt() { return _smoothed_rtt; }

private:
    uint32_t _latest_rtt;
    uint32_t _rtt_var;
    uint32_t _min_rtt;
    uint32_t _smoothed_rtt;

    uint64_t _last_update_time; 

};

}

#endif