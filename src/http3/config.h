// Copyright (c) 2024 The quicX Authors. All rights reserved.
// HTTP/3 layer centralized configuration constants

#ifndef HTTP3_CONFIG_H
#define HTTP3_CONFIG_H

#include <cstdint>
#include <string>

namespace quicx {
namespace http3 {

// ============================================================================
// HTTP/3 Protocol Configuration
// ============================================================================

// HTTP/3 ALPN identifier (RFC 9114 Section 3.1)
// Used during TLS handshake to negotiate HTTP/3 protocol
static const std::string kHttp3Alpn = "h3";

// ============================================================================
// HTTP/3 Connection Configuration
// ============================================================================

// Timeout before destroying connection after GOAWAY/close (milliseconds)
// 1 second allows graceful shutdown of in-flight requests
// Used in: http/client.cpp
static constexpr uint32_t kConnectionCloseDestroyTimeoutMs = 1000;

// Maximum server push wait time (milliseconds)
// Timeout for waiting on promised push streams
// Note: Server push is optional in HTTP/3; this controls client-side timeout
static constexpr uint32_t kServerPushWaitTimeMs = 10;  // 10 milliseconds

// Client connection idle timeout (milliseconds)
// Connection closed if no activity for this duration
static constexpr uint32_t kClientConnectionTimeoutMs = 60000;  // 60 seconds

// ============================================================================
// HTTP/3 Frame Configuration
// ============================================================================

// Maximum DATA frame payload size (bytes)
// 1350 bytes accounts for QUIC overhead:
//   MTU 1472 - QUIC header (~13B) - AEAD tag (16B) - HTTP/3 frame header (~5B)
//   - conservative margin (~88B) = ~1350B
// This avoids IP fragmentation while maximizing payload efficiency
// Optimized from original 1400 to better fit within path MTU
// Used in: req_resp_base_stream.cpp
static constexpr uint32_t kMaxDataFramePayload = 1350;

// ============================================================================
// HTTP/3 Stream Lifecycle
// ============================================================================

// Period of the per-IConnection cleanup timer that drains
// streams_to_destroy_ and runs the RFC 9114 §5.2 graceful-drain probe.
// 100 ms is a working default: it bounds the worst-case latency between
// "last in-flight stream finishes" and "CONNECTION_CLOSE goes out", but
// does not poll so often as to show up in CPU profiles. This is a
// per-connection tick — total cost scales with the active connection
// count, not request rate.
// Note (roadmap): make this configurable via Http3Config once the
// connection-config plumbing lands; the loop already supports defer.
// Tracked in learning_project_roadmap.md §2.
static constexpr uint32_t kStreamCleanupIntervalMs = 100;

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_CONFIG_H
