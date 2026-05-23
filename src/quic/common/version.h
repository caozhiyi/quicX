#ifndef QUIC_COMMON_VERSION
#define QUIC_COMMON_VERSION

#include <cstddef>
#include <cstdint>
#include <vector>

#include <quicx/quic/type.h>  // for kQuicVersion1 / kQuicVersion2

// NOTE: The on-the-wire QUIC protocol version constants
// (kQuicVersion1 / kQuicVersion2) are now defined in the public header
// <quicx/quic/type.h>. This internal header keeps the helper functions and
// the supported-versions table.

namespace quicx {
namespace quic {

// Supported versions in preference order (most preferred first)
static const uint32_t kQuicVersions[] = {
    kQuicVersion2,  // QUIC v2 (RFC 9369) - preferred
    kQuicVersion1,  // QUIC v1 (RFC 9000)
};

// Number of supported versions
static constexpr size_t kQuicVersionsCount = sizeof(kQuicVersions) / sizeof(kQuicVersions[0]);

// Check if a version is supported
bool VersionCheck(uint32_t version);

// Check if version is QUIC v2
inline bool IsQuicV2(uint32_t version) { return version == kQuicVersion2; }

// Check if version is QUIC v1
inline bool IsQuicV1(uint32_t version) { return version == kQuicVersion1; }

// Get preferred version from a list of versions
// Returns 0 if no compatible version found
uint32_t SelectVersion(const std::vector<uint32_t>& versions);

// Get the default (most preferred) version
inline uint32_t GetDefaultVersion() { return kQuicVersions[0]; }

// Get version string for logging
const char* VersionToString(uint32_t version);

}  // namespace quic
}  // namespace quicx

#endif