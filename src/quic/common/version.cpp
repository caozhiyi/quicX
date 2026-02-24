#include <cstddef>
#include <cstdint>
#include "quic/common/version.h"

namespace quicx {
namespace quic {

bool VersionCheck(uint32_t version) {
    for (size_t i = 0; i < kQuicVersionsCount; i++) {
        if (kQuicVersions[i] == version) {
            return true;
        }
    }
    return false;
}

uint32_t SelectVersion(const std::vector<uint32_t>& versions) {
    // Select the first mutually supported version in our preference order
    for (size_t i = 0; i < kQuicVersionsCount; i++) {
        for (auto version : versions) {
            if (kQuicVersions[i] == version) {
                return version;
            }
        }
    }
    return 0;  // No compatible version found
}

const char* VersionToString(uint32_t version) {
    switch (version) {
        case kQuicVersion1: return "QUICv1";
        case kQuicVersion2: return "QUICv2";
        default: return "Unknown";
    }
}

}  // namespace quic
}  // namespace quicx
