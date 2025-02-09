#include <cstdint>
#include "quic/common/version.h"

namespace quicx {
namespace quic {

bool VersionCheck(uint32_t version) {
    static uint16_t versions_size = (sizeof(kQuicVersions) / sizeof(kQuicVersions[0]));
    for (uint16_t i = 0; i < versions_size; i++) {
        if (kQuicVersions[i] == version) {
            return true;
        }
    }

    return false;
}

}
}
