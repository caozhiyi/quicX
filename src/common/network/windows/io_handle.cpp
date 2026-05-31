#ifdef _WIN32
// Windows networking headers (winsock2 first). No need to include windows.h directly.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <atomic>
#include <string>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/network/socket_family_cache.h"

namespace quicx {
namespace common {

namespace {
// Resolve the address family of `sockfd`. Cache hit covers all UDP fds
// we created; falls back to getsockname() (the only portable Windows
// path — there is no SO_DOMAIN on winsock).
int32_t ResolveSocketFamily(int32_t sockfd) {
    int32_t fam = GetSocketFamily(sockfd);
    if (fam != 0) return fam;

    sockaddr_storage ss;
    int ss_len = sizeof(ss);
    if (getsockname(sockfd, reinterpret_cast<sockaddr*>(&ss), &ss_len) == 0) {
        return ss.ss_family;
    }
    return AF_INET;
}
}  // namespace

// Retrieve WSARecvMsg function pointer at runtime (not always declared by headers)
static LPFN_WSARECVMSG ResolveWSARecvMsg(SOCKET sockfd) {
    static LPFN_WSARECVMSG wsa_recv_msg = nullptr;
    static bool initialized = false;
    if (!initialized) {
        GUID guid = WSAID_WSARECVMSG;
        DWORD bytes = 0;
        if (WSAIoctl(sockfd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &wsa_recv_msg,
                sizeof(wsa_recv_msg), &bytes, NULL, NULL) == SOCKET_ERROR) {
            wsa_recv_msg = nullptr;
        }
        initialized = true;
    }
    return wsa_recv_msg;
}

SysCallInt32Result TcpSocket() {
    int32_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    return {sock, sock != INVALID_SOCKET ? 0 : WSAGetLastError()};
}

UdpSocketResult UdpSocket() {
    // NOTE: the Windows path historically created a v4-only socket here.
    // We preserve that behavior to keep WSARecvMsg/Send paths working
    // unchanged; SendTo() below also formats sockaddr_in directly. If
    // dual-stack support is added later, this is the place to switch
    // to AF_INET6 + IPV6_V6ONLY=0.
    int32_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != INVALID_SOCKET) {
        RememberSocketFamily(sock, AF_INET);
        SetUdpSocketBuffer(sock, kDefaultUdpBufferSize);
        return {sock, 0, AF_INET};
    }
    return {-1, WSAGetLastError(), 0};
}

UdpSocketResult UdpSocket4() {
    // Create an IPv4-only UDP socket (AF_INET).
    int32_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != INVALID_SOCKET) {
        RememberSocketFamily(sock, AF_INET);
        SetUdpSocketBuffer(sock, kDefaultUdpBufferSize);
        return {sock, 0, AF_INET};
    }
    return {-1, WSAGetLastError(), 0};
}

SysCallInt32Result Close(int32_t sockfd) {
    ForgetSocketFamily(sockfd);
    const int32_t rc = closesocket(sockfd);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Bind(int32_t sockfd, int32_t sock_family, Address& addr) {
    if (sock_family == AF_INET6) {
        // IPv6 / dual-stack socket
        struct sockaddr_in6 addr_in6;
        memset(&addr_in6, 0, sizeof(addr_in6));
        addr_in6.sin6_family = AF_INET6;
        addr_in6.sin6_port = htons(addr.GetPort());

        if (addr.GetAddressType() == AddressType::kIpv6 || addr.GetIp() == "::") {
            // Pure IPv6 address (or wildcard)
            if (addr.GetIp() == "::" || addr.GetIp().empty()) {
                addr_in6.sin6_addr = in6addr_any;
            } else {
                inet_pton(AF_INET6, addr.GetIp().c_str(), &addr_in6.sin6_addr);
            }
        } else {
            // For IPv4 addresses on a dual-stack socket, use IPv4-mapped IPv6 address ::ffff:x.x.x.x
            if (addr.GetIp() == "0.0.0.0" || addr.GetIp().empty()) {
                addr_in6.sin6_addr = in6addr_any;
            } else {
                std::string mapped = "::ffff:" + addr.GetIp();
                inet_pton(AF_INET6, mapped.c_str(), &addr_in6.sin6_addr);
            }
        }

        const int32_t rc = bind(sockfd, reinterpret_cast<sockaddr*>(&addr_in6), sizeof(addr_in6));
        return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
    }

    // AF_INET (default for TCP and IPv4 UDP sockets)
    struct sockaddr_in addr_in;
    memset(&addr_in, 0, sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(addr.GetPort());
    if (addr.GetIp() == "0.0.0.0" || addr.GetIp().empty()) {
        addr_in.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, addr.GetIp().c_str(), &addr_in.sin_addr);
    }
    const int32_t rc = bind(sockfd, (sockaddr*)&addr_in, sizeof(addr_in));
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Bind(int32_t sockfd, Address& addr) {
    return Bind(sockfd, ResolveSocketFamily(sockfd), addr);
}

SysCallInt32Result Accept(int32_t sockfd, Address& addr) {
    struct sockaddr_in addr_cli;
    socklen_t fromlen = sizeof(sockaddr);

    const int32_t rc = accept(sockfd, (sockaddr*)&addr_cli, &fromlen);
    if (rc == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAECONNABORTED) {
            return {-1, 0};
        }
        return {-1, WSAGetLastError()};
    }
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_cli.sin_addr, ip, sizeof(ip));
    addr.SetIp(ip);
    addr.SetPort(ntohs(addr_cli.sin_port));
    addr.SetAddressType(Address::CheckAddressType(addr.GetIp()));
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Listen(int32_t sockfd, int32_t backlog) {
    const int32_t rc = listen(sockfd, backlog);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Write(int32_t sockfd, const char* data, uint32_t len) {
    const int32_t rc = send(sockfd, data, len, 0);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Writev(int32_t sockfd, Iovec* vec, uint32_t vec_len) {
    DWORD bytes_sent;
    const int32_t rc = WSASend(sockfd, (WSABUF*)vec, vec_len, &bytes_sent, 0, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SendTo(int32_t sockfd, const char* msg, uint32_t len, uint16_t flag, const Address& addr) {
    // PERF (P1): cached sockaddr fast path. Windows path historically only
    // creates AF_INET UDP sockets (see UdpSocket()), so we always cache
    // under AF_INET here.
    socklen_t cached_len = 0;
    if (const struct sockaddr* cached = addr.GetCachedSockaddr(AF_INET, cached_len)) {
        const int32_t rc = sendto(sockfd, msg, len, flag, cached, cached_len);
        return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
    }

    struct sockaddr_in addr_cli;
    memset(&addr_cli, 0, sizeof(addr_cli));
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(addr.GetPort());
    inet_pton(AF_INET, addr.GetIp().c_str(), &addr_cli.sin_addr);
    addr.StoreCachedSockaddr(AF_INET, (struct sockaddr*)&addr_cli, sizeof(addr_cli));

    const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_cli, sizeof(addr_cli));
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SendMsg(int32_t sockfd, const Msghdr* msg, int16_t flag) {
    DWORD bytes_sent;
    const int32_t rc = WSASendMsg(sockfd, (LPWSAMSG)msg, flag, &bytes_sent, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SendmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag) {
    DWORD bytes_sent;
    int32_t rc = 0;
    for (uint32_t i = 0; i < vlen; ++i) {
        rc = WSASendMsg(sockfd, (LPWSAMSG)&msgvec[i].msg_hdr_, flag, &bytes_sent, NULL, NULL);
        if (rc == SOCKET_ERROR) {
            return {rc, WSAGetLastError()};
        }
        msgvec[i].msg_len_ = bytes_sent;
    }
    return {rc, 0};
}

// Windows has its own UDP segmentation offload (URO/USO via WSASendMsg
// + WSAUDP_SEND_MSG_SIZE). For now we don't wire it up — return EIO so
// the caller (UdpSender::SendBatch) permanently disables GSO and falls
// back to the sendmmsg-emulation path above.
SysCallInt32Result SendMsgGso(int32_t /*sockfd*/,
                              const char* /*payload*/, uint32_t /*total_len*/,
                              uint16_t /*segment_size*/,
                              const Address& /*addr*/) {
    return {-1, EIO};
}

SysCallInt32Result Recv(int32_t sockfd, char* data, uint32_t len, uint16_t flag) {
    const int32_t rc = recv(sockfd, data, len, flag);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Readv(int32_t sockfd, Iovec* vec, uint32_t vec_len) {
    DWORD bytes_received;
    const int32_t rc = WSARecv(sockfd, (WSABUF*)vec, vec_len, &bytes_received, NULL, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result RecvFrom(int32_t sockfd, char* msg, uint32_t len, uint16_t flag, Address& addr) {
    struct sockaddr_in addr_cli;
    int addr_len = sizeof(addr_cli);
    const int32_t rc = recvfrom(sockfd, msg, len, flag, (sockaddr*)&addr_cli, &addr_len);
    if (rc != SOCKET_ERROR) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr_cli.sin_addr, ip, sizeof(ip));
        addr.SetIp(ip);
        addr.SetPort(ntohs(addr_cli.sin_port));
    }
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result RecvMsg(int32_t sockfd, Msghdr* msg, int16_t flag) {
    DWORD bytes_received;
    LPFN_WSARECVMSG fn = ResolveWSARecvMsg((SOCKET)sockfd);
    if (!fn) {
        return {SOCKET_ERROR, WSAEOPNOTSUPP};
    }
    const int32_t rc = fn((SOCKET)sockfd, (LPWSAMSG)msg, &bytes_received, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result RecvmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out) {
    // Windows has no recvmmsg; we emulate it with a WSARecvMsg loop and
    // mirror Linux's "count" semantics: the result is the number of
    // datagrams successfully read (NOT a byte total), so the
    // cross-platform RecvFromBatch wrapper can consume it uniformly.
    //
    // WSAEWOULDBLOCK is the loop's natural stop when the socket is
    // non-blocking and momentarily drained: return {i, 0} so the caller
    // gets the count it has already collected. Any other error is real
    // and is propagated alongside the partial count.
    LPFN_WSARECVMSG fn = ResolveWSARecvMsg((SOCKET)sockfd);
    if (!fn) {
        return {0, WSAEOPNOTSUPP};
    }
    DWORD bytes_received;
    for (uint32_t i = 0; i < vlen; ++i) {
        int32_t rc = fn((SOCKET)sockfd, (LPWSAMSG)&msgvec[i].msg_hdr_, &bytes_received, NULL, NULL);
        if (rc == SOCKET_ERROR) {
            int wsa_err = WSAGetLastError();
            if (wsa_err == WSAEWOULDBLOCK) {
                return {static_cast<int32_t>(i), 0};
            }
            return {static_cast<int32_t>(i), wsa_err};
        }
        msgvec[i].msg_len_ = bytes_received;
    }
    return {static_cast<int32_t>(vlen), 0};
}

SysCallInt32Result SetSockOpt(int32_t sockfd, int level, int optname, const void* optval, uint32_t optlen) {
    const int32_t rc = setsockopt(sockfd, level, optname, (const char*)optval, optlen);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SetUdpSocketBuffer(int32_t sockfd, int32_t size_bytes) {
    static std::atomic<bool> warned_rcvbuf{false};
    static std::atomic<bool> warned_sndbuf{false};

    // Winsock honors SO_RCVBUF/SO_SNDBUF as set without doubling, capped
    // by per-process / per-system limits (which are typically generous).
    const int32_t warn_threshold = (size_bytes * 7) / 10;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&size_bytes, sizeof(size_bytes));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&size_bytes, sizeof(size_bytes));

    int actual_rcv = 0, actual_snd = 0;
    int len = sizeof(actual_rcv);
    getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&actual_rcv, &len);
    len = sizeof(actual_snd);
    getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&actual_snd, &len);

    if (actual_rcv < warn_threshold && !warned_rcvbuf.exchange(true)) {
        LOG_WARN("UDP SO_RCVBUF clamped to %d bytes (requested %d). "
                 "Packets may be dropped under load.", actual_rcv, size_bytes);
    }
    if (actual_snd < warn_threshold && !warned_sndbuf.exchange(true)) {
        LOG_WARN("UDP SO_SNDBUF clamped to %d bytes (requested %d). "
                 "Sends may stall under load.", actual_snd, size_bytes);
    }
    return {actual_rcv, 0};
}

SysCallInt32Result SocketNoblocking(int32_t sockfd) {
    u_long mode = 1;
    const int32_t rc = ioctlsocket(sockfd, FIONBIO, &mode);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
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
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    int rc = getaddrinfo(host.c_str(), NULL, &hints, &res);
    if (rc != 0) {
        return false;
    }

    struct sockaddr_in* addr_in = (struct sockaddr_in*)res->ai_addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ip, sizeof(ip));
    addr.SetIp(ip);
    addr.SetPort(ntohs(addr_in->sin_port));

    freeaddrinfo(res);
    return true;
}

bool Pipe(int32_t& pipe1, int32_t& pipe2) {
    // Windows does not have pipe() for sockets, so we use a pair of connected sockets (AF_INET, SOCK_STREAM)
    SOCKET listen_sock = INVALID_SOCKET, sock1 = INVALID_SOCKET, sock2 = INVALID_SOCKET;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        return false;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let system choose port

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listen_sock);
        return false;
    }

    if (listen(listen_sock, 1) == SOCKET_ERROR) {
        closesocket(listen_sock);
        return false;
    }

    if (getsockname(listen_sock, (struct sockaddr*)&addr, &addrlen) == SOCKET_ERROR) {
        closesocket(listen_sock);
        return false;
    }

    sock1 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock1 == INVALID_SOCKET) {
        closesocket(listen_sock);
        return false;
    }

    if (connect(sock1, (struct sockaddr*)&addr, addrlen) == SOCKET_ERROR) {
        closesocket(listen_sock);
        closesocket(sock1);
        return false;
    }

    sock2 = accept(listen_sock, NULL, NULL);
    if (sock2 == INVALID_SOCKET) {
        closesocket(listen_sock);
        closesocket(sock1);
        return false;
    }

    closesocket(listen_sock);

    pipe1 = sock1;
    pipe2 = sock2;
    return true;
}

SysCallInt32Result EnableUdpEcn(int32_t sockfd) {
    (void)sockfd;
    // TODO: Implement via WSARecvMsg ancillary data options if needed
    return {0, 0};
}

SysCallInt32Result RecvFromWithEcn(
    int32_t sockfd, char* buf, uint32_t len, uint16_t flag, Address& addr, uint8_t& ecn) {
    // Fallback to RecvFrom without ECN on Windows for now
    auto ret = RecvFrom(sockfd, buf, len, flag, addr);
    ecn = 0;
    return ret;
}

SysCallInt32Result EnableUdpEcnMarking(int32_t sockfd, uint8_t ecn_codepoint) {
    (void)sockfd;
    (void)ecn_codepoint;
    // TODO: implement IP_TOS/TrafficClass on Windows if required
    return {0, 0};
}

}  // namespace common
}  // namespace quicx

#endif
