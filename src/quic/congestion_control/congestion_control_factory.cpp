#include "quic/congestion_control/bbr_v1_congestion_control.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"
#include "quic/congestion_control/bbr_v3_congestion_control.h"
#include "quic/congestion_control/congestion_control_factory.h"
#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/reno_congestion_control.h"

#include "common/log/log.h"

#include "quic/config.h"

#include <algorithm>
#include <cctype>

namespace quicx {
namespace quic {

namespace {
std::string ToLower(const std::string& s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}  // namespace

CongestionControlType CongestionControlTypeFromString(const std::string& name) {
    // Empty means "not configured": use the compile-time default.
    std::string v = ToLower(name.empty() ? std::string(kDefaultCongestionControl) : name);
    if (v == "cubic") {
        return CongestionControlType::kCubic;
    } else if (v == "bbrv1" || v == "bbr" || v == "bbr1") {
        return CongestionControlType::kBbrV1;
    } else if (v == "bbrv2" || v == "bbr2") {
        return CongestionControlType::kBbrV2;
    } else if (v == "bbrv3" || v == "bbr3") {
        return CongestionControlType::kBbrV3;
    } else if (v == "reno") {
        return CongestionControlType::kReno;
    }
    LOG_WARN("CongestionControlTypeFromString: unknown CC algorithm \"%s\", falling back to reno", name.c_str());
    return CongestionControlType::kReno;
}

const char* CongestionControlTypeToString(CongestionControlType type) {
    switch (type) {
        case CongestionControlType::kCubic:
            return "cubic";
        case CongestionControlType::kReno:
            return "reno";
        case CongestionControlType::kBbrV1:
            return "bbrv1";
        case CongestionControlType::kBbrV2:
            return "bbrv2";
        case CongestionControlType::kBbrV3:
            return "bbrv3";
        default:
            return "unknown";
    }
}

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

}  // namespace quic
}  // namespace quicx
