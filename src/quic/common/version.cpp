#include "quic/common/version.h"

namespace quicx {

bool CheckVersion(uint32_t version) {
    static uint16_t versions_size = (sizeof(__quic_versions) / sizeof(__quic_versions[0]));
    for (uint16_t i = 0; i < versions_size; i++) {
        if (__quic_versions[i] == version) {
            return true;
        }
    }

    return false;
}

}
