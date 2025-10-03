#ifndef QUIC_CONGESTION_CONTROL_FACTORY
#define QUIC_CONGESTION_CONTROL_FACTORY

#include <memory>
#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

enum class CongestionControlType {
    kCubic,
    kBbrV1,
    kBbrV2,
    kBbrV3,
    kReno
};

std::unique_ptr<ICongestionControl> CreateCongestionControl(CongestionControlType type);

} // namespace quic
} // namespace quicx

#endif // QUIC_CONGESTION_CONTROL_FACTORY_H