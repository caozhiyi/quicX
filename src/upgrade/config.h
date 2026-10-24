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

// Maximum time a connection may linger AFTER the upgrade outcome is decided
// (response fully sent). Pre-existing behaviour left UPGRADED and FAILED
// connections registered on the loop forever: nothing processed their data
// and nothing ever closed them — one permanently leaked fd (plus epoll map
// entry) per abandoned upgrade (P0-S8). The completion handler re-arms the
// connection's timer with this value as a bounded linger, after which the fd
// is closed.
static constexpr uint64_t kUpgradePostCompleteLingerMs = 30000;  // 30 s

// Cap on bytes buffered while trying to classify the connection
// (DETECTING/NEGOTIATING). The detector needs at most ~1 KiB of prefix; the
// old code appended peer data to initial_data without bound — a hostile peer
// could park gigabytes of memory per connection. Exceeding the cap fails the
// upgrade (which closes the connection).
static constexpr size_t kUpgradeInitialDataLimit = 8 * 1024;

}  // namespace upgrade
}  // namespace quicx

#endif  // UPGRADE_CONFIG_H
