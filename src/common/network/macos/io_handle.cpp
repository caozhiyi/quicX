#ifdef __APPLE__
#include <netdb.h> 
#include <errno.h>
#include <fcntl.h>
#include <atomic>
#include <unistd.h>       // for close
#include <ifaddrs.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/network/socket_family_cache.h"

namespace quicx {
namespace common {

namespace {
// Resolve the address family of `sockfd`, preferring our own creation-time
// cache over any syscall. Returns 0 (AF_UNSPEC) when truly unknown — the
// caller will then have to make a best-effort guess.
int32_t ResolveSocketFamily(int32_t sockfd) {
    int32_t fam = GetSocketFamily(sockfd);
    if (fam != 0) return fam;

    // Not tracked by us (e.g. caller-provided fd, or a TCP fd): fall back
    // to getsockname(). On macOS this works even for unbound sockets — the
    // kernel reports the socket's create-time family.
    struct sockaddr_storage ss;
    socklen_t ss_len = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if (getsockname(sockfd, (struct sockaddr*)&ss, &ss_len) == 0 && ss.ss_family != 0) {
        return ss.ss_family;
    }

    // Last resort: probe IPV6_V6ONLY. If gettable, it's an IPv6 socket.
    int v6only = 0;
    socklen_t v6only_len = sizeof(v6only);
    if (getsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &v6only_len) == 0) {
        return AF_INET6;
    }
    return AF_INET;
}
}  // namespace

SysCallInt32Result TcpSocket() {
    int32_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    return {sock, sock != -1 ? 0 : errno};
}

UdpSocketResult UdpSocket() {
    int32_t sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != -1) {
        // Enable dual-stack: allow IPv4 connections on IPv6 socket.
        int off = 0;
        setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        RememberSocketFamily(sock, AF_INET6);
        SetUdpSocketBuffer(sock, kDefaultUdpBufferSize);
        return {sock, 0, AF_INET6};
    }
    // Fallback to IPv4-only if IPv6 is not available.
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != -1) {
        RememberSocketFamily(sock, AF_INET);
        SetUdpSocketBuffer(sock, kDefaultUdpBufferSize);
        return {sock, 0, AF_INET};
    }
    return {-1, errno, 0};
}

UdpSocketResult UdpSocket4() {
    // Create an IPv4-only UDP socket (AF_INET).
    // Used for connection migration when peer is IPv4, to avoid IPv6 dual-stack
    // routing issues in certain network environments (e.g., Docker bridge networks).
    int32_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != -1) {
        RememberSocketFamily(sock, AF_INET);
        SetUdpSocketBuffer(sock, kDefaultUdpBufferSize);
        return {sock, 0, AF_INET};
    }
    return {-1, errno, 0};
}

SysCallInt32Result Close(int32_t sockfd) {
    ForgetSocketFamily(sockfd);
    const int32_t rc = close(sockfd);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Bind(int32_t sockfd, int32_t sock_family, Address& addr) {
    if (sock_family == AF_INET6) {
        struct sockaddr_in6 addr_in6;
        memset(&addr_in6, 0, sizeof(addr_in6));
        addr_in6.sin6_family = AF_INET6;
        addr_in6.sin6_port = htons(addr.GetPort());

        if (addr.GetAddressType() == AddressType::kIpv6 || addr.GetIp() == "::" || addr.GetIp().find(':') != std::string::npos) {
            // Pure IPv6 address (or wildcard).
            if (addr.GetIp() == "::" || addr.GetIp().empty()) {
                addr_in6.sin6_addr = in6addr_any;
            } else {
                inet_pton(AF_INET6, addr.GetIp().c_str(), &addr_in6.sin6_addr);
            }
        } else {
            // IPv4 address on dual-stack socket: use IPv4-mapped IPv6 address.
            if (addr.GetIp() == "0.0.0.0" || addr.GetIp().empty()) {
                addr_in6.sin6_addr = in6addr_any;
            } else {
                std::string mapped = "::ffff:" + addr.GetIp();
                inet_pton(AF_INET6, mapped.c_str(), &addr_in6.sin6_addr);
            }
        }
        const int32_t rc = bind(sockfd, (sockaddr*)&addr_in6, sizeof(addr_in6));
        return {rc, rc != -1 ? 0 : errno};
    }

    // AF_INET (or anything else: treat as v4 — Bind() of a non-INET socket
    // here would be a programmer error anyway).
    struct sockaddr_in addr_in;
    memset(&addr_in, 0, sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(addr.GetPort());
    if (addr.GetIp() == "0.0.0.0" || addr.GetIp().empty()) {
        addr_in.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr_in.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());
    }
    const int32_t rc = bind(sockfd, (sockaddr*)&addr_in, sizeof(addr_in));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Bind(int32_t sockfd, Address& addr) {
    return Bind(sockfd, ResolveSocketFamily(sockfd), addr);
}

SysCallInt32Result Accept(int32_t sockfd, Address& addr) {
    struct sockaddr_storage addr_storage;
    socklen_t fromlen = sizeof(addr_storage);

    const int32_t rc = accept(sockfd, (sockaddr*)&addr_storage, &fromlen);
    if (rc == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == ECONNABORTED) {
            return {-1, 0};
        }
        return {-1, errno};
    }

    char ipstr[INET6_ADDRSTRLEN] = {0};
    if (addr_storage.ss_family == AF_INET) {
        auto* addr_in = (struct sockaddr_in*)&addr_storage;
        inet_ntop(AF_INET, &addr_in->sin_addr, ipstr, sizeof(ipstr));
        addr.SetIp(ipstr);
        addr.SetPort(ntohs(addr_in->sin_port));
        addr.SetAddressType(AddressType::kIpv4);
    } else if (addr_storage.ss_family == AF_INET6) {
        auto* addr_in6 = (struct sockaddr_in6*)&addr_storage;
        if (IN6_IS_ADDR_V4MAPPED(&addr_in6->sin6_addr)) {
            struct in_addr v4addr;
            memcpy(&v4addr, &addr_in6->sin6_addr.s6_addr[12], 4);
            inet_ntop(AF_INET, &v4addr, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(addr_in6->sin6_port));
            addr.SetAddressType(AddressType::kIpv4);
        } else {
            inet_ntop(AF_INET6, &addr_in6->sin6_addr, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(addr_in6->sin6_port));
            addr.SetAddressType(AddressType::kIpv6);
        }
    }
    return {rc, 0};
}

SysCallInt32Result Listen(int32_t sockfd, int32_t backlog) {
    const int32_t rc = listen(sockfd, backlog);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Write(int32_t sockfd, const char *data, uint32_t len) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    // when send return -1, errno is ENOTSOCK, it means the socket is not connected, we should use write instead.
    int32_t rc = send(sockfd, data, len, flags);
    if (rc == -1 && errno == ENOTSOCK) {
        rc = write(sockfd, data, len);
    }
    return {rc, rc != -1 ? 0 : errno};
}
SysCallInt32Result Writev(int32_t sockfd, Iovec *vec, uint32_t vec_len) {
    const int32_t rc = writev(sockfd, (iovec*)vec, vec_len);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendTo(int32_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr) {
    // Resolve once (O(1) cache hit on the hot path; falls back to a single
    // syscall only for fds we didn't create ourselves, e.g. test fixtures).
    const int32_t sock_family = ResolveSocketFamily(sockfd);
    const bool is_ipv6_socket = (sock_family == AF_INET6);

    // PERF (P1): consult Address-side cache. The (sock_family) tag is enough
    // because the same Address object talks to one peer for its lifetime
    // (caller is the connection's send path), so we only need to keep
    // distinct cached sockaddr_storage per family flavor (AF_INET for pure
    // IPv4 socket, AF_INET6 for IPv6 / dual-stack socket).
    const int cache_family = is_ipv6_socket ? AF_INET6 : AF_INET;
    socklen_t cached_len = 0;
    if (const struct sockaddr* cached = addr.GetCachedSockaddr(cache_family, cached_len)) {
        const int32_t rc = sendto(sockfd, msg, len, flag, cached, cached_len);
        return {rc, rc != -1 ? 0 : errno};
    }

    bool is_ipv6_addr = (addr.GetAddressType() == AddressType::kIpv6 || addr.GetIp().find(':') != std::string::npos);

    if (is_ipv6_addr) {
        // Pure IPv6 destination
        struct sockaddr_in6 addr_in6;
        memset(&addr_in6, 0, sizeof(addr_in6));
        addr_in6.sin6_family = AF_INET6;
        addr_in6.sin6_port = htons(addr.GetPort());
        inet_pton(AF_INET6, addr.GetIp().c_str(), &addr_in6.sin6_addr);
        addr.StoreCachedSockaddr(AF_INET6, (struct sockaddr*)&addr_in6, sizeof(addr_in6));
        const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_in6, sizeof(addr_in6));
        return {rc, rc != -1 ? 0 : errno};
    }

    if (is_ipv6_socket) {
        // IPv4 destination on dual-stack socket: use IPv4-mapped IPv6 address
        struct sockaddr_in6 addr_in6;
        memset(&addr_in6, 0, sizeof(addr_in6));
        addr_in6.sin6_family = AF_INET6;
        addr_in6.sin6_port = htons(addr.GetPort());
        std::string mapped = "::ffff:" + addr.GetIp();
        inet_pton(AF_INET6, mapped.c_str(), &addr_in6.sin6_addr);
        addr.StoreCachedSockaddr(AF_INET6, (struct sockaddr*)&addr_in6, sizeof(addr_in6));
        const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_in6, sizeof(addr_in6));
        return {rc, rc != -1 ? 0 : errno};
    }

    // Pure IPv4 socket
    struct sockaddr_in addr_cli;
    memset(&addr_cli, 0, sizeof(addr_cli));
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(addr.GetPort());
    addr_cli.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());
    addr.StoreCachedSockaddr(AF_INET, (struct sockaddr*)&addr_cli, sizeof(addr_cli));

    const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_cli, sizeof(addr_cli));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendMsg(int32_t sockfd, const Msghdr* msg, int16_t flag) {
    const int32_t rc = sendmsg(sockfd, (msghdr*)msg, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag) {
    // [perf-verify a] macOS has no native sendmmsg(2); previously this
    // returned {0,0}, which the cross-platform call sites interpret as
    // "primitive not available" and silently fall back to a per-packet
    // SendTo() loop in their *outer* dispatcher. That fallback re-enters
    // the dispatcher per packet, paying full Address resolution + msghdr
    // staging cost each time.
    //
    // This implementation walks the same `msgvec` in a tight loop using
    // sendmsg(2) — same syscall count as the fallback (UDP sendmsg can
    // only deliver one datagram per call on macOS) but skips the upper
    // dispatcher overhead and writes msg_len_ in-place, matching the
    // Linux sendmmsg semantic. Returns the number of datagrams
    // successfully sent (matching sendmmsg's contract).
    if (msgvec == nullptr || vlen == 0) {
        return {0, 0};
    }
    uint32_t i = 0;
    int last_errno = 0;
    for (; i < vlen; ++i) {
        const int32_t rc = sendmsg(sockfd, (msghdr*)&msgvec[i].msg_hdr_, flag);
        if (rc < 0) {
            last_errno = errno;
            // EAGAIN/EWOULDBLOCK on the very first packet -> propagate as
            // -1 like sendmmsg does. After at least one success, stop and
            // report the count of successes.
            if (i == 0) {
                return {-1, last_errno};
            }
            break;
        }
        msgvec[i].msg_len_ = static_cast<uint32_t>(rc);
    }
    return {static_cast<int32_t>(i), last_errno};
}

// macOS has no UDP GSO (UDP_SEGMENT is Linux-specific). Return a sentinel
// errno that makes the caller (UdpSender::SendBatch) permanently disable
// the GSO path on first attempt and fall back to sendmmsg.
SysCallInt32Result SendMsgGso(int32_t /*sockfd*/,
                              const char* /*payload*/, uint32_t /*total_len*/,
                              uint16_t /*segment_size*/,
                              const Address& /*addr*/) {
    return {-1, EIO};
}

SysCallInt32Result Recv(int32_t sockfd, char *data, uint32_t len, uint16_t flag) {
    const int32_t rc = recv(sockfd, data, len, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Readv(int32_t sockfd, Iovec *vec, uint32_t vec_len) {
    const int32_t rc = readv(sockfd, (iovec*)vec, vec_len);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result RecvFrom(int32_t sockfd, char *buf, uint32_t len, uint16_t flag, Address& addr) {
    struct sockaddr_storage addr_storage;
    socklen_t fromlen = sizeof(addr_storage);

    const int32_t rc = recvfrom(sockfd, buf, len, 0, (sockaddr*)&addr_storage, &fromlen);
    if (rc == -1) {
        return {rc, errno};
    }
    
    char ipstr[INET6_ADDRSTRLEN] = {0};
    if (addr_storage.ss_family == AF_INET) {
        auto* addr_in = (struct sockaddr_in*)&addr_storage;
        inet_ntop(AF_INET, &addr_in->sin_addr, ipstr, sizeof(ipstr));
        addr.SetIp(ipstr);
        addr.SetPort(ntohs(addr_in->sin_port));
        addr.SetAddressType(AddressType::kIpv4);
    } else if (addr_storage.ss_family == AF_INET6) {
        auto* addr_in6 = (struct sockaddr_in6*)&addr_storage;
        // Check for IPv4-mapped IPv6 address (::ffff:x.x.x.x)
        if (IN6_IS_ADDR_V4MAPPED(&addr_in6->sin6_addr)) {
            // Extract the IPv4 address from the mapped address
            struct in_addr v4addr;
            memcpy(&v4addr, &addr_in6->sin6_addr.s6_addr[12], 4);
            inet_ntop(AF_INET, &v4addr, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(addr_in6->sin6_port));
            addr.SetAddressType(AddressType::kIpv4);
        } else {
            inet_ntop(AF_INET6, &addr_in6->sin6_addr, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(addr_in6->sin6_port));
            addr.SetAddressType(AddressType::kIpv6);
        }
    }
    return {rc, 0};
}

SysCallInt32Result RecvMsg(int32_t sockfd, Msghdr* msg, int16_t flag) {
    const int32_t rc = recvmsg(sockfd, (msghdr*)msg, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result RecvmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out) {
    // Linux's recvmmsg(2) returns the number of datagrams it managed to
    // read. macOS has no recvmmsg, so we emulate it with a recvmsg loop
    // and DELIBERATELY mirror Linux's "count" semantics (NOT a byte
    // total), because the cross-platform RecvFromBatch wrapper consumes
    // the result as a count.
    //
    // EAGAIN / EWOULDBLOCK is the loop's natural stop condition when the
    // caller passes MSG_DONTWAIT to drain a non-blocking socket: the
    // socket is empty, we've collected `i` datagrams already, return
    // {i, 0}. Any other errno is a real error and we propagate it
    // alongside whatever count we'd already accumulated so the caller
    // can still process those packets.
    for (uint32_t i = 0; i < vlen; ++i) {
        int32_t rc = recvmsg(sockfd, (msghdr*)&msgvec[i].msg_hdr_, flag);
        if (rc == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {static_cast<int32_t>(i), 0};
            }
            return {static_cast<int32_t>(i), errno};
        }
        msgvec[i].msg_len_ = rc;
    }
    return {static_cast<int32_t>(vlen), 0};
}

SysCallInt32Result SetSockOpt(int32_t sockfd, int level, int optname, const void *optval, uint32_t optlen) {
    const int32_t rc = setsockopt(sockfd, level, optname, optval, optlen);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SetUdpSocketBuffer(int32_t sockfd, int32_t size_bytes) {
    // One-time warning across all sockets in the process so 500 client
    // sockets don't print 500 identical warnings on a stock-config box.
    static std::atomic<bool> warned_rcvbuf{false};
    static std::atomic<bool> warned_sndbuf{false};

    // BSD/Darwin (unlike Linux) does NOT silently double the requested
    // value, but it caps at `kern.ipc.maxsockbuf` (default ~2 MiB).
    // quic-go docs note BSD adds ~15% padding to its own kernel limit,
    // so a 4 MiB request usually returns 4 MiB until you raise
    // maxsockbuf. We warn at <70% of requested.
    const int32_t warn_threshold = (size_bytes * 7) / 10;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size_bytes, sizeof(size_bytes));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &size_bytes, sizeof(size_bytes));

    int actual_rcv = 0, actual_snd = 0;
    socklen_t len = sizeof(actual_rcv);
    getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &actual_rcv, &len);
    len = sizeof(actual_snd);
    getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &actual_snd, &len);

    if (actual_rcv < warn_threshold && !warned_rcvbuf.exchange(true)) {
        // BSD/Darwin needs ~15% headroom on top of the desired size.
        const int32_t suggested_kern_limit = static_cast<int32_t>(size_bytes * 1.15);
        LOG_WARN("UDP SO_RCVBUF clamped to %d bytes (requested %d). "
                 "Packets may be dropped under load. "
                 "Run as root: sysctl -w kern.ipc.maxsockbuf=%d",
                 actual_rcv, size_bytes, suggested_kern_limit);
    }
    if (actual_snd < warn_threshold && !warned_sndbuf.exchange(true)) {
        const int32_t suggested_kern_limit = static_cast<int32_t>(size_bytes * 1.15);
        LOG_WARN("UDP SO_SNDBUF clamped to %d bytes (requested %d). "
                 "Sends may stall under load. "
                 "Run as root: sysctl -w kern.ipc.maxsockbuf=%d",
                 actual_snd, size_bytes, suggested_kern_limit);
    }
    return {actual_rcv, 0};
}

SysCallInt32Result SocketNoblocking(int32_t sockfd) {
    int32_t old_option = fcntl(sockfd, F_GETFL);
    int32_t new_option = old_option | O_NONBLOCK;
    const int32_t rc = fcntl(sockfd, F_SETFL, new_option);
    return {rc, rc != -1 ? 0 : errno};
}

bool ParseRemoteAddress(uint16_t fd, Address& addr) {
    struct sockaddr_in addr_in;
    socklen_t addr_len = sizeof(addr_in);

    if (getpeername(fd, (struct sockaddr*)&addr_in, &addr_len) == -1) {
        return false;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in.sin_addr, ip_str, sizeof(ip_str));
    addr.SetIp(ip_str);
    addr.SetPort(ntohs(addr_in.sin_port));
    return true;
}

bool ParseLocalAddress(int32_t fd, Address& addr) {
    struct sockaddr_in addr_in;
    socklen_t addr_len = sizeof(addr_in);

    if (getsockname(fd, (struct sockaddr*)&addr_in, &addr_len) == -1) {
        return false;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in.sin_addr, ip_str, sizeof(ip_str));
    addr.SetIp(ip_str);
    addr.SetPort(ntohs(addr_in.sin_port));
    return true;
}

bool LookupAddress(const std::string& host, Address& addr) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    
    // Check if host is an explicit IPv4 address (contains dots and no colons)
    bool is_ipv4_literal = (host.find('.') != std::string::npos && host.find(':') == std::string::npos);
    
    // If it's an IPv4 literal or looks like one, prefer IPv4
    hints.ai_family = is_ipv4_literal ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM; // Datagram socket for UDP
    hints.ai_flags = AI_PASSIVE;    // For wildcard IP address

    struct addrinfo *result;
    int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (ret != 0) {
        return false;
    }

    // Prefer IPv4 addresses over IPv6 for better compatibility
    struct addrinfo *ipv4_addr = nullptr;
    struct addrinfo *ipv6_addr = nullptr;
    
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET && !ipv4_addr) {
            ipv4_addr = rp;
        } else if (rp->ai_family == AF_INET6 && !ipv6_addr) {
            ipv6_addr = rp;
        }
    }
    
    // Use IPv4 if available, otherwise use IPv6
    struct addrinfo *selected = ipv4_addr ? ipv4_addr : ipv6_addr;
    if (!selected) {
        freeaddrinfo(result);
        return false;
    }
    
    void *addr_ptr;
    char ip_str[INET6_ADDRSTRLEN];
    
    if (selected->ai_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)selected->ai_addr;
        addr_ptr = &(ipv4->sin_addr);
        addr.SetAddressType(AddressType::kIpv4);
    } else {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)selected->ai_addr;
        addr_ptr = &(ipv6->sin6_addr);
        addr.SetAddressType(AddressType::kIpv6);
    }
    
    inet_ntop(selected->ai_family, addr_ptr, ip_str, sizeof(ip_str));
    addr.SetIp(ip_str);
    freeaddrinfo(result);
    return true;
}

bool Pipe(int32_t& pipe1, int32_t& pipe2) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        return false;
    }
    pipe1 = sv[0];
    pipe2 = sv[1];
    return true;
}

SysCallInt32Result EnableUdpEcn(int32_t sockfd) {
    int on = 1;
    int rc1 = setsockopt(sockfd, IPPROTO_IP, IP_RECVTOS, &on, sizeof(on));
#ifdef IPV6_RECVTCLASS
    int rc2 = setsockopt(sockfd, IPPROTO_IPV6, IPV6_RECVTCLASS, &on, sizeof(on));
#else
    int rc2 = 0;
#endif
    int ok = (rc1 == -1 || rc2 == -1) ? -1 : 0;
    return {ok, ok != -1 ? 0 : errno};
}

SysCallInt32Result RecvFromWithEcn(int32_t sockfd, char *buf, uint32_t len, uint16_t flag, Address& addr, uint8_t& ecn) {
    struct sockaddr_storage addr_ss; memset(&addr_ss, 0, sizeof(addr_ss));
    struct iovec iov; iov.iov_base = (void*)buf; iov.iov_len = len;
    char cbuf[128]; memset(cbuf, 0, sizeof(cbuf));
    struct msghdr msg; memset(&msg, 0, sizeof(msg));
    msg.msg_name = &addr_ss; msg.msg_namelen = sizeof(addr_ss);
    msg.msg_iov = &iov; msg.msg_iovlen = 1;
    msg.msg_control = cbuf; msg.msg_controllen = sizeof(cbuf);

    int32_t rc = recvmsg(sockfd, (msghdr*)&msg, flag);
    if (rc == -1) {
        return {rc, errno};
    }
    // address
    char ipstr[INET6_ADDRSTRLEN] = {0};
    if (addr_ss.ss_family == AF_INET) {
        auto* sin = (struct sockaddr_in*)&addr_ss;
        inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof(ipstr));
        addr.SetIp(ipstr);
        addr.SetPort(ntohs(sin->sin_port));
        addr.SetAddressType(AddressType::kIpv4);
    } else if (addr_ss.ss_family == AF_INET6) {
        auto* sin6 = (struct sockaddr_in6*)&addr_ss;
        // Check for IPv4-mapped IPv6 address (::ffff:x.x.x.x)
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            struct in_addr v4addr;
            memcpy(&v4addr, &sin6->sin6_addr.s6_addr[12], 4);
            inet_ntop(AF_INET, &v4addr, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(sin6->sin6_port));
            addr.SetAddressType(AddressType::kIpv4);
        } else {
            inet_ntop(AF_INET6, &sin6->sin6_addr, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(sin6->sin6_port));
            addr.SetAddressType(AddressType::kIpv6);
        }
    }
    // ECN
    ecn = 0;
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR((msghdr*)&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR((msghdr*)&msg, cmsg)) {
        if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS) {
            int tos = 0; memcpy(&tos, CMSG_DATA(cmsg), sizeof(tos));
            ecn = static_cast<uint8_t>(tos & 0x03);
        }
#ifdef IPV6_TCLASS
        if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_TCLASS) {
            int tclass = 0; memcpy(&tclass, CMSG_DATA(cmsg), sizeof(tclass));
            ecn = static_cast<uint8_t>(tclass & 0x03);
        }
#endif
    }
    return {rc, 0};
}

SysCallInt32Result EnableUdpEcnMarking(int32_t sockfd, uint8_t ecn_codepoint) {
    int tos = static_cast<int>(ecn_codepoint & 0x03);
    int rc1 = setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
#ifdef IPV6_TCLASS
    int tclass = static_cast<int>(ecn_codepoint & 0x03);
    int rc2 = setsockopt(sockfd, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass));
#else
    int rc2 = 0;
#endif
    int ok = (rc1 == -1 || rc2 == -1) ? -1 : 0;
    return {ok, ok != -1 ? 0 : errno};
}

}
}

#endif