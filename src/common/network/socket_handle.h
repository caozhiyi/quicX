#ifndef COMMON_NETWORK_SOCKET_HANDLE
#define COMMON_NETWORK_SOCKET_HANDLE

#include <cstdint>

namespace quicx {
namespace common {

/**
 * A socket fd bundled with the address family it was created with.
 *
 * The family is known at every socket-creation site (UdpSocket() /
 * UdpSocket4() return it in UdpSocketResult::family_) but was historically
 * discarded the moment the fd was stored, then re-derived later from the
 * kernel (SO_DOMAIN / getsockname) or from a process-global fd->family map.
 * Carrying it next to the fd instead makes that recovery machinery
 * unnecessary: the value simply travels with its owner.
 *
 * Layout: 8-byte trivially-copyable POD, cheap to store in hot-path
 * structs (NetPacket) and to pass by value.
 *
 * Conventions:
 *   - fd == 0 / fd < 0: "no socket" (mirrors the historical int32_t
 *     sentinels used across the codebase).
 *   - family == 0 (AF_UNSPEC): family unknown — e.g. an fd injected from
 *     outside the process's own creation paths. Consumers fall back to
 *     ResolveSocketFamily() (one syscall) for such fds.
 */
struct SocketHandle {
    int32_t fd{0};
    int32_t family{0};

    SocketHandle() = default;
    SocketHandle(int32_t fd_, int32_t family_): fd(fd_), family(family_) {}
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_NETWORK_SOCKET_HANDLE
