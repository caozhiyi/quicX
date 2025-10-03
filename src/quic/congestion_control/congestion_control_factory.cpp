#include "quic/congestion_control/reno_congestion_control.h"
#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/bbr_v1_congestion_control.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"
#include "quic/congestion_control/bbr_v3_congestion_control.h"
#include "quic/congestion_control/congestion_control_factory.h"

namespace quicx {
namespace quic {

std::unique_ptr<ICongestionControl> CreateCongestionControl(CongestionControlType type) {
    switch (type) {
        case CongestionControlType::kCubic:
            return std::unique_ptr<CubicCongestionControl>(new CubicCongestionControl());
        case CongestionControlType::kReno:
            return std::unique_ptr<RenoCongestionControl>(new RenoCongestionControl());
        case CongestionControlType::kBbrV1:
            return std::unique_ptr<BBRv1CongestionControl>(new BBRv1CongestionControl());
        case CongestionControlType::kBbrV2:
            return std::unique_ptr<BBRv2CongestionControl>(new BBRv2CongestionControl());
        case CongestionControlType::kBbrV3:
            return std::unique_ptr<BBRv3CongestionControl>(new BBRv3CongestionControl());
        default:
            return nullptr;
    }
}

}
}
