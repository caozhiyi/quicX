#ifndef QUIC_CONGESTION_CONTROL_FACTORY
#define QUIC_CONGESTION_CONTROL_FACTORY

#include <memory>
#include <string>

#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

enum class CongestionControlType { kCubic, kBbrV1, kBbrV2, kBbrV3, kReno };

std::unique_ptr<ICongestionControl> CreateCongestionControl(CongestionControlType type);

// Parse a CC algorithm name ("cubic" / "reno" / "bbrv1" / "bbrv2" / "bbrv3",
// case-insensitive; "bbr"/"bbr1"/"bbr2"/"bbr3" aliases accepted) into a
// CongestionControlType. An empty string yields the compile-time default
// (kDefaultCongestionControl in quic/config.h). Unknown values fall back to
// reno with a LOG_WARN, matching the historical SendControl behaviour.
CongestionControlType CongestionControlTypeFromString(const std::string& name);

// Canonical name for a type (for logging / qlog).
const char* CongestionControlTypeToString(CongestionControlType type);

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONGESTION_CONTROL_FACTORY