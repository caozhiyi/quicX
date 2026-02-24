#ifndef QUIC_COMMON_VERSION
#define QUIC_COMMON_VERSION

#include <cstddef>
#include <cstdint>
#include <vector>

namespace quicx {
namespace quic {

// QUIC Version Constants (RFC 9000, RFC 9369)
static constexpr uint32_t kQuicVersion1 = 0x00000001;  // QUIC v1 (RFC 9000)
static constexpr uint32_t kQuicVersion2 = 0x6b3343cf;  // QUIC v2 (RFC 9369)

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