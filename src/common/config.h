// Copyright (c) 2024 The quicX Authors. All rights reserved.
// common layer centralized configuration constants

#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace quicx {
namespace common {

// ============================================================================
// Log Configuration
// ============================================================================

// Number of log entries cached before flush
static constexpr uint16_t kLogCacheSize = 20;

// Size of a single log block; also the max formatted line length (bytes)
static constexpr uint16_t kLogBlockSize = 2048;

// Initial reserved capacity (bytes) of the thread-local log-context tag
// buffer (e.g. "[conn:1234][strm:5678]...")
static constexpr size_t kDefaultContextSize = 256;

// ============================================================================
// Allocator Configuration
// ============================================================================

// Largest object size (bytes) served by PoolAllocator's free-list path.
// Larger allocations fall through to the backing NormalAllocator (malloc).
static constexpr uint32_t kDefaultMaxBytes = 256;

// Nodes added per ReFill() when a PoolAllocator free list runs dry.
static constexpr uint32_t kDefaultNumberAddNodes = 20;

// Max blocks retained in BlockMemoryPool's free list before blocks are
// returned to the OS.
static constexpr uint16_t kMaxBlockNum = 20;

// ============================================================================
// Network Configuration
// ============================================================================

// Default UDP socket buffer size requested for every QUIC UDP socket.
// 4 MiB is the "good middle ground" used by Chromium/QUICHE example servers
// and is plenty for hundreds of concurrent QUIC connections at LAN/loopback
// rates. quic-go documents up to 7.5 MB for very high-bandwidth WAN paths;
// applications may call SetUdpSocketBuffer() again with a larger value.
//
// NOTE: Linux clamps this to `min(2*requested, net.core.{r,w}mem_max)`. So
// on a stock Linux box (rmem_max=212992 by default) the kernel will silently
// truncate to ~208 KiB and SetUdpSocketBuffer() will print a one-time
// warning suggesting `sysctl -w net.core.rmem_max=...`.
static constexpr int32_t kDefaultUdpBufferSize = 4 * 1024 * 1024;  // 4 MiB

// Hard cap on per-call receive batch size inside RecvFromBatch(). Beyond this
// point the syscall benefit plateaus and the stack frame grows uncomfortably
// (one kCmsgPerDgram cmsg buffer per datagram plus iovec/mmsghdr/sockaddr
// arrays -> ~64 KiB worst case). Callers above (quic/config.h kMaxRecvBatch)
// must stay <= this value; it also sizes OnRead's stack arrays.
static constexpr uint32_t kRecvBatchHardCap = 256;

// Bytes of cmsg scratch reserved per datagram. IP_TOS / IPV6_TCLASS each
// occupy 16-24 bytes including alignment; 128 bytes is generous and keeps us
// safe if we add IP_PKTINFO / IPV6_PKTINFO later.
static constexpr size_t kCmsgPerDgram = 128;

}  // namespace common
}  // namespace quicx

#endif  // COMMON_CONFIG_H
