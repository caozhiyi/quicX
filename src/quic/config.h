// Copyright (c) 2024 The quicX Authors. All rights reserved.
// QUIC layer centralized configuration constants

#ifndef QUIC_CONFIG_H
#define QUIC_CONFIG_H

#include <cstdint>

namespace quicx {
namespace quic {

// ============================================================================
// Packet Layer Configuration
// ============================================================================

// Default destination Connection ID length for short header packets (RFC 9000 Section 17.3)
// 20 bytes is the maximum CID length, used when encoding short headers
// Replaces hardcoded value in: src/quic/packet/header/short_header.cpp
static constexpr uint32_t kDefaultDestinationCidLength = 20;

// Maximum payload size for a single frame (bytes)
// 1420 bytes fits within MTU 1500 after QUIC header + AEAD tag + IP/UDP headers
static constexpr uint32_t kMaxFramePayload = 1420;

// ============================================================================
// Connection-level Flow Control
// ============================================================================

// Bytes remaining before sending DATA_BLOCKED to peer (RFC 9000 Section 4.1)
// 16KB (~11 MTU packets) provides sufficient advance warning
// Optimized from original 8912 to align with standard buffer sizes
// Used in: send_flow_controller.h
static constexpr uint64_t kDataBlockedThreshold = 16384;

// Streams remaining before sending STREAMS_BLOCKED to peer
// Used in: send_flow_controller.h
static constexpr uint64_t kStreamsBlockedThreshold = 4;

// Bytes remaining before sending MAX_DATA to increase peer's send limit
// Used in: recv_flow_controller.h
static constexpr uint64_t kDataIncreaseThreshold = 512 * 1024;  // 512KB

// Amount to increase connection-level data limit in MAX_DATA frame (RFC 9000 Section 4.2)
// 2MB provides ample headroom for high-throughput transfers
// Used in: recv_flow_controller.h
static constexpr uint64_t kDataIncreaseAmount = 2 * 1024 * 1024;  // 2MB

// Streams remaining before proactively sending MAX_STREAMS
// Used in: recv_flow_controller.h
static constexpr uint64_t kStreamsIncreaseThreshold = 4;

// Amount to increase MAX_STREAMS limit per frame
// Used in: recv_flow_controller.h
static constexpr uint64_t kStreamsIncreaseAmount = 10;

// Maximum ACK delay in milliseconds (RFC 9000 Section 18.2, default 25ms)
// Balances ACK frequency with protocol overhead
// Used in: send_control.cpp
static constexpr uint32_t kMaxAckDelay = 25;

// ============================================================================
// Stream-level Flow Control
// ============================================================================

// Bytes remaining before sending STREAM_DATA_BLOCKED to peer
// 4KB (~3 MTU packets) provides reasonable buffer before blocking
// Optimized from original 2048 for high-throughput scenarios
// Used in: send_stream.cpp
static constexpr uint64_t kStreamDataBlockedThreshold = 4096;

// Amount to increase stream-level receive window in MAX_STREAM_DATA frame
// 2MB aligns with connection-level window increment
// Used in: recv_stream.cpp
static constexpr uint64_t kStreamWindowIncrement = 2 * 1024 * 1024;  // 2MB

// Amount to increase stream window when peer sends STREAM_DATA_BLOCKED
// 4MB provides burst capacity for large file transfers
// Used in: recv_stream.cpp
static constexpr uint64_t kBlockedWindowIncrement = 4 * 1024 * 1024;  // 4MB

// Maximum stream-level receive window size (hard upper limit)
// Prevents malicious peers from inflating window indefinitely via BLOCKED frames
// Used in: recv_stream.cpp
static constexpr uint64_t kMaxStreamWindowSize = 64 * 1024 * 1024;  // 64MB

// Maximum number of out-of-order frames buffered per stream
// Prevents OOM from malicious peers sending many different-offset frames
// Used in: recv_stream.cpp
static constexpr size_t kMaxOutOfOrderFrames = 1024;

// ============================================================================
// TLS/Crypto Configuration
// ============================================================================

// Default TLS peer certificate verification setting
// false = skip verification (for testing/dev), true = verify peer cert (production)
// Note: This is a default; actual behavior controlled by QuicConfig::enable_tls_verify_
// Used in: tls_ctx_client.cpp, tls_connection_client.cpp
static constexpr bool kDefaultTlsVerifyPeer = false;

// ============================================================================
// Server Configuration
// ============================================================================

// Handshake timeout in milliseconds
// 5 seconds provides reasonable time for TLS handshake even on slow networks
// Used in: worker_server.cpp
static constexpr uint32_t kHandshakeTimeoutMs = 5000;

// Retry token Connection ID length (RFC 9000 Section 8.1)
// 8 bytes is the minimum recommended CID length for retry tokens
// Used in: worker_server.cpp
static constexpr uint32_t kRetryCidLength = 8;

// Packet pool size (number of pre-allocated packet buffers)
// 256 (power of 2) is optimized for memory pool management and high concurrency
// Increased from original 200 to reduce allocation overhead under load
// Used in: pool_pakcet_allotor.cpp
static constexpr uint32_t kPacketPoolSize = 256;

// Individual packet buffer size (bytes)
// 1500 bytes matches typical Ethernet MTU
static constexpr uint32_t kPacketBufferSize = 1500;

// Number of blocks in packet pool allocator
// 64 blocks balances memory overhead with allocation efficiency
static constexpr uint32_t kPacketPoolBlockCount = 64;

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONFIG_H
