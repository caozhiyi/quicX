#ifndef QUIC_COMMON_VERSION
#define QUIC_COMMON_VERSION

#include <string>

namespace quicx {

static const uint32_t __quic_versions[] = {
    0x00000001, // QUICv1
};

bool CheckVersion(uint32_t version);

}

#endif