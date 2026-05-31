// Shared test fixture helpers for integration tests.
//
// Background:
//   The integration tests bring up a real HTTP/3 server on 127.0.0.1 with a
//   per-test UDP port. When stale processes (e.g. a previous run that
//   crashed under a debugger) keep one of those ports busy, bind() fails
//   silently inside the server's master event loop, which used to manifest
//   as per-test 5-second hangs (the fixture would unconditionally sleep
//   500 ms and then issue requests against a server whose UDP socket was
//   never bound).
//
// The single helper this file provides is `ProbeFreeUdpPort`, which uses a
// throw-away UDP socket to confirm that a candidate port is currently free
// before the fixture hands it to the server's `Start()`. Combined with
// running `Start()` synchronously on the main thread (so its return value
// is checked via `ASSERT_TRUE`), this turns "stale port held by zombie
// process" from an opaque request timeout into an immediate, actionable
// fixture ASSERT.

#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace quicx {
namespace test {

// Probe `next_port` (and successors) until we find a UDP port that the
// kernel is willing to bind on `ip` (default 127.0.0.1), then close the
// probe socket and return that port. This is a pragmatic guard against
// stale processes (or unrelated services) holding ports inside the
// test's reserved range.
//
// There is a tiny race between closing the probe socket and the server
// re-binding the same port, but in practice it's harmless and far better
// than the alternative (per-test 5-second silent timeouts).
//
// Returns the bound port on success, or 0 on failure (caller should
// ASSERT on the result).
inline uint16_t ProbeFreeUdpPort(std::atomic<uint16_t>& next_port,
                                 const std::string& ip = "127.0.0.1",
                                 int max_attempts = 64) {
#if defined(_WIN32)
    using fd_t = SOCKET;
    constexpr fd_t kInvalidFd = INVALID_SOCKET;
    auto close_fd = [](fd_t f) { ::closesocket(f); };
#else
    using fd_t = int;
    constexpr fd_t kInvalidFd = -1;
    auto close_fd = [](fd_t f) { ::close(f); };
#endif

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        uint16_t candidate = next_port.fetch_add(1);

        fd_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd == kInvalidFd) {
            return 0;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(candidate);
#if defined(_WIN32)
        ::InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr);
#else
        ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
#endif

        int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        close_fd(fd);

        if (rc == 0) {
            return candidate;
        }
        // bind failed (most likely EADDRINUSE) -- try the next candidate.
    }
    return 0;
}

}  // namespace test
}  // namespace quicx
