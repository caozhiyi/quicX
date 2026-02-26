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
static constexpr uint32_t kServerPushWaitTimeMs = 30000;  // 30 seconds

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

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_CONFIG_H
