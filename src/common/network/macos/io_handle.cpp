#ifdef __APPLE__
#include <netdb.h> 
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>       // for close
#include <ifaddrs.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include "common/network/io_handle.h"

namespace quicx {
namespace common {

SysCallInt32Result TcpSocket() {
    int32_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    return {sock, sock != -1 ? 0 : errno};
}

SysCallInt32Result UdpSocket() {
    int32_t sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        // Fallback to IPv4 if IPv6 is not available
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        return {sock, sock != -1 ? 0 : errno};
    }
    // Enable dual-stack: allow IPv4 connections on IPv6 socket
    int off = 0;
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    return {sock, 0};
}

SysCallInt32Result Close(int32_t sockfd) {
    const int32_t rc = close(sockfd);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Bind(int32_t sockfd, Address& addr) {
    // Determine actual socket family via getsockname (works on macOS even before bind)
    int sock_family = AF_INET;
    struct sockaddr_storage ss;
    socklen_t ss_len = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if (getsockname(sockfd, (struct sockaddr*)&ss, &ss_len) == 0 && ss.ss_family != 0) {
        sock_family = ss.ss_family;
    } else {
        // getsockname may return AF_UNSPEC before bind; try creating a test to detect
        // For AF_INET6 sockets, binding with sockaddr_in6 works; for AF_INET, sockaddr_in.
        // Heuristic: try IPv6 first if address looks like IPv6
        if (addr.GetAddressType() == AddressType::kIpv6 || addr.GetIp() == "::" || addr.GetIp().find(':') != std::string::npos) {
            sock_family = AF_INET6;
        } else {
            // Try to detect socket family by attempting a dummy getsockopt
            // On macOS, we can check if IPV6_V6ONLY is gettable
            int v6only = 0;
            socklen_t v6only_len = sizeof(v6only);
            if (getsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &v6only_len) == 0) {
                sock_family = AF_INET6;
            }
        }
    }

    if (sock_family == AF_INET6) {
        struct sockaddr_in6 addr_in6;
        memset(&addr_in6, 0, sizeof(addr_in6));
        addr_in6.sin6_family = AF_INET6;
        addr_in6.sin6_port = htons(addr.GetPort());

        if (addr.GetAddressType() == AddressType::kIpv6 || addr.GetIp() == "::" || addr.GetIp().find(':') != std::string::npos) {
            // Pure IPv6 address
            if (addr.GetIp() == "::" || addr.GetIp().empty()) {
                addr_in6.sin6_addr = in6addr_any;
            } else {
                inet_pton(AF_INET6, addr.GetIp().c_str(), &addr_in6.sin6_addr);
            }
        } else {
            // IPv4 address on dual-stack socket: use IPv4-mapped IPv6 address
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
    
    // IPv4 socket
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
    // Detect socket family to handle dual-stack correctly
    bool is_ipv6_socket = false;
    int v6only = 0;
    socklen_t v6only_len = sizeof(v6only);
    if (getsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &v6only_len) == 0) {
        is_ipv6_socket = true;
    }

    bool is_ipv6_addr = (addr.GetAddressType() == AddressType::kIpv6 || addr.GetIp().find(':') != std::string::npos);

    if (is_ipv6_addr) {
        // Pure IPv6 destination
        struct sockaddr_in6 addr_in6;
        memset(&addr_in6, 0, sizeof(addr_in6));
        addr_in6.sin6_family = AF_INET6;
        addr_in6.sin6_port = htons(addr.GetPort());
        inet_pton(AF_INET6, addr.GetIp().c_str(), &addr_in6.sin6_addr);
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
        const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_in6, sizeof(addr_in6));
        return {rc, rc != -1 ? 0 : errno};
    }

    // Pure IPv4 socket
    struct sockaddr_in addr_cli;
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(addr.GetPort());
    addr_cli.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());

    const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_cli, sizeof(addr_cli));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendMsg(int32_t sockfd, const Msghdr* msg, int16_t flag) {
    const int32_t rc = sendmsg(sockfd, (msghdr*)msg, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag) {
    return {0, 0};
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
    int32_t total_rc = 0;
    for (uint32_t i = 0; i < vlen; ++i) {
        int32_t rc = recvmsg(sockfd, (msghdr*)&msgvec[i].msg_hdr_, flag);
        if (rc == -1) {
            return {rc, errno};
        }
        msgvec[i].msg_len_ = rc;
        total_rc += rc;
    }
    return {total_rc, 0};
}

SysCallInt32Result SetSockOpt(int32_t sockfd, int level, int optname, const void *optval, uint32_t optlen) {
    const int32_t rc = setsockopt(sockfd, level, optname, optval, optlen);
    return {rc, rc != -1 ? 0 : errno};
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