#ifndef QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR
#define QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR

#include <cstdint>

namespace quicx {
namespace quic {

const static uint32_t kInitRtt = 250;

class RttCalculator {
public:
    RttCalculator();
    ~RttCalculator();

    bool UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay);

    void Reset();

    uint32_t GetPT0Interval(uint32_t max_ack_delay);

    uint32_t GetLatestRtt() { return latest_rtt_; }
    uint32_t GetRttVar() { return rtt_var_; }
    uint32_t GetMinRtt() { return min_rtt_; }
    uint32_t GetSmoothedRtt() { return smoothed_rtt_; }

private:
    uint32_t latest_rtt_;
    uint32_t rtt_var_;
    uint32_t min_rtt_;
    uint32_t smoothed_rtt_;

    uint64_t last_update_time_; 
};

}
}

#endif