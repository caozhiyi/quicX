#ifndef QUIC_CONGESTION_CONTROL_INTERFACE
#define QUIC_CONGESTION_CONTROL_INTERFACE

#include <cstdint>

namespace quicx {

class ICongestionControl {
public:
    ICongestionControl();
    ~ICongestionControl();

    virtual bool CanSend(uint32_t& can_send_size) = 0;

    virtual void OnPacketSend(uint64_t send_time, uint32_t send_bytes, uint64_t pkt_num) = 0;
};

}

#endif