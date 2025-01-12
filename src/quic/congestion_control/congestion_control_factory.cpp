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
            return std::unique_ptr<CubicCongestionControl>(new CubicCongestionControl());
        case CCT_RENO:
            return std::unique_ptr<RenoCongestionControl>(new RenoCongestionControl());
        case CCT_BBR_V1:
            return std::unique_ptr<BBRv1CongestionControl>(new BBRv1CongestionControl());
        case CCT_BBR_V2:
            return std::unique_ptr<BBRv2CongestionControl>(new BBRv2CongestionControl());
        default:
            return nullptr;
    }
}

} // namespace quic
} // namespace quicx
