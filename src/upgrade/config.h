// Copyright (c) 2024 The quicX Authors. All rights reserved.
// upgrade layer centralized configuration constants

#ifndef UPGRADE_CONFIG_H
#define UPGRADE_CONFIG_H

#include <cstdint>

namespace quicx {
namespace upgrade {

// ============================================================================
// Upgrade Negotiation
// ============================================================================

// Maximum time we allow a TCP-accepted but not-yet-classified connection to
// remain in the "negotiation" phase (waiting for the first bytes that will
// disambiguate HTTP/1.1 vs HTTP/2 vs TLS / HTTP/3 upgrade). After this the
// fd is closed to free resources and cap exposure to slow-loris-style
// connections that never send a request line.
//
// This is an engineering default, not a protocol value: it is much larger
// than typical HTTP idle timeouts because some clients legitimately defer
// the first byte for several seconds (e.g. proxies, mobile networks).
static constexpr uint64_t kUpgradeNegotiationTimeoutMs = 30000;  // 30 s

}  // namespace upgrade
}  // namespace quicx

#endif  // UPGRADE_CONFIG_H
