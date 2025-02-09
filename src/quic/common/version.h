#ifndef QUIC_COMMON_VERSION
#define QUIC_COMMON_VERSION

#include <string>

namespace quicx {
namespace quic {

static const uint32_t kQuicVersions[] = {
    0x00000001, // QUICv1
};

bool VersionCheck(uint32_t version);

}
}

#endif