#ifndef QUIC_CONGESTION_CONTROL_FACTORY
#define QUIC_CONGESTION_CONTROL_FACTORY

#include <memory>
#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

enum CongestionControlType {
    CCT_CUBIC,
    CCT_BBR_V1,
    CCT_BBR_V2,
    CCT_RENO
};

std::unique_ptr<ICongestionControl> Create(CongestionControlType type);

} // namespace quic
} // namespace quicx

#endif // QUIC_CONGESTION_CONTROL_FACTORY_H