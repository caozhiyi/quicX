#ifndef QUIC_CONGESTION_CONTROL_LOSS_DETECTION
#define QUIC_CONGESTION_CONTROL_LOSS_DETECTION

#include <cstdint>

namespace quicx {
namespace quic {

// TODO 在ack时检测丢包
class LossDetection {
public:
    LossDetection();
    ~LossDetection();

    void OnPacketSend();

private:

};

}
}

#endif