#include "quic/congestion_control/reno_congestion_control.h"
#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/bbr_v1_congestion_control.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"
#include "quic/congestion_control/congestion_control_factory.h"

namespace quicx {
namespace quic {

std::unique_ptr<ICongestionControl> Create(CongestionControlType type) {
    switch (type) {
        case CCT_CUBIC:
            return std::make_unique<CubicCongestionControl>();
        case CCT_RENO:
            return std::make_unique<RenoCongestionControl>();
        case CCT_BBR_V1:
            return std::make_unique<BBRv1CongestionControl>();
        case CCT_BBR_V2:
            return std::make_unique<BBRv2CongestionControl>();
        default:
            return nullptr;
    }
}

} // namespace quic
} // namespace quicx
