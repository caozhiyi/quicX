#ifndef COMMON_NETWORK_SOCKET_FAMILY_CACHE
#define COMMON_NETWORK_SOCKET_FAMILY_CACHE

#include <cstdint>

namespace quicx {
namespace common {

// Process-wide map from UDP socket fd -> address family (AF_INET / AF_INET6).
//
// Background:
//   POSIX gives no portable, cheap way to ask an existing fd "are you
//   AF_INET or AF_INET6?" — Linux has SO_DOMAIN, macOS does not, Windows
//   neither. Previously every Bind()/SendTo() probed via getsockname() or
//   getsockopt(IPV6_V6ONLY), which (a) was a syscall on the hot path and
//   (b) returned ambiguous results for unbound sockets on macOS.
//
// Design:
//   We are the ones who create UDP sockets (UdpSocket / UdpSocket4), so
//   we already know the family at creation time. Recording it here turns
//   later "what family is fd?" lookups into an O(1) lock-protected hash
//   read with no syscalls. Close() removes the entry to prevent fd reuse
//   from returning a stale family.
//
// Notes:
//   * Only UDP fds created by UdpSocket*() are tracked. TCP fds, pipe
//     fds, externally-supplied fds, etc. are NOT in the cache; callers
//     that fall back to the auto-detecting Bind() overload still work
//     for those (just at the cost of a syscall, which is fine because
//     those paths are not hot).
//   * GetSocketFamily() returns 0 (which is neither AF_INET nor AF_INET6)
//     when the fd is not tracked, so callers can detect "unknown" and
//     decide to probe the fd themselves.
void RememberSocketFamily(int32_t fd, int32_t family);
void ForgetSocketFamily(int32_t fd);
int32_t GetSocketFamily(int32_t fd);  // returns 0 if unknown

}  // namespace common
}  // namespace quicx

#endif  // COMMON_NETWORK_SOCKET_FAMILY_CACHE
