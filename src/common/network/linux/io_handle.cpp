#ifdef __linux__
#include <netdb.h>
#include <fcntl.h>
#include <cstring>
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
    int32_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return {sock, sock != -1 ? 0 : errno};
}

SysCallInt32Result Close(int32_t sockfd) {
    const int32_t rc = close(sockfd);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Bind(int32_t sockfd, Address& addr) {
    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(addr.GetPort());
    addr_in.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());

    const int32_t rc = bind(sockfd, (sockaddr*)&addr_in, sizeof(addr_in));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Accept(int32_t sockfd, Address& addr) {
    struct sockaddr_in addr_cli;
    socklen_t addr_len = sizeof(addr_cli);
    const int32_t rc = accept(sockfd, (sockaddr*)&addr_cli, &addr_len);
    if (rc == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == ECONNABORTED) {
            return {-1, 0};
        }
        return {-1, errno};
    }
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_cli.sin_addr, ip, sizeof(ip));
    addr.SetIp(ip);
    addr.SetPort(ntohs(addr_cli.sin_port));
    addr.SetAddressType(Address::CheckAddressType(addr.GetIp()));
    return {rc, 0};
}

SysCallInt32Result Listen(int32_t sockfd, int32_t backlog) {
    const int32_t rc = listen(sockfd, backlog);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Write(int32_t sockfd, const char *data, uint32_t len) {
    const int32_t rc = write(sockfd, data, len);
    return {rc, rc != -1 ? 0 : errno};
}
SysCallInt32Result Writev(int32_t sockfd, Iovec *vec, uint32_t vec_len) {
    const int32_t rc = writev(sockfd, (iovec*)vec, vec_len);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendTo(int32_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr) {
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
    const int32_t rc = sendmmsg(sockfd, (mmsghdr*)msgvec, vlen, flag);
    return {rc, rc != -1 ? 0 : errno};
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
    struct sockaddr_in addr_cli;
    socklen_t fromlen = sizeof(sockaddr);

    const int32_t rc = recvfrom(sockfd, buf, len, flag, (sockaddr*)&addr_cli, &fromlen);
    if (rc == -1) {
        return {rc, errno};
    }

    addr.SetIp(inet_ntoa(addr_cli.sin_addr));
    addr.SetPort(ntohs(addr_cli.sin_port));
    addr.SetAddressType(Address::CheckAddressType(addr.GetIp()));
    return {rc, 0};
}

SysCallInt32Result RecvMsg(int32_t sockfd, Msghdr* msg, int16_t flag) {
    const int32_t rc = recvmsg(sockfd, (msghdr*)msg, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result RecvmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out) {
    timespec time;
    time.tv_sec = time_out / 1000;

    const int32_t rc = recvmmsg(sockfd, (mmsghdr*)msgvec, vlen, flag, &time);
    return {rc, rc != -1 ? 0 : errno};
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

bool LookupAddress(const std::string& host, Address& addr) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    
    hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM; // Datagram socket for UDP
    hints.ai_flags = AI_PASSIVE;    // For wildcard IP address

    struct addrinfo *result;
    int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (ret != 0) {
        return false;
    }

    // Get the first valid address
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        void *addr_ptr;
        char ip_str[INET6_ADDRSTRLEN];

        if (rp->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr_ptr = &(ipv4->sin_addr);
            addr.SetAddressType(AddressType::kIpv4);
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr_ptr = &(ipv6->sin6_addr);
            addr.SetAddressType(AddressType::kIpv6);
        }

        // Convert IP to string
        inet_ntop(rp->ai_family, addr_ptr, ip_str, sizeof(ip_str));
        addr.SetIp(ip_str);
        freeaddrinfo(result);
        return true;
    }

    freeaddrinfo(result);
    return false;
}

bool Pipe(int32_t& pipe1, int32_t& pipe2) {
    int fds[2];
    if (pipe(fds) == -1) {
        return false;
    }
    pipe1 = fds[0];
    pipe2 = fds[1];
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

    int32_t rc = recvmsg(sockfd, &msg, flag);
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
        inet_ntop(AF_INET6, &sin6->sin6_addr, ipstr, sizeof(ipstr));
        addr.SetIp(ipstr);
        addr.SetPort(ntohs(sin6->sin6_port));
        addr.SetAddressType(AddressType::kIpv6);
    }
    // ECN
    ecn = 0;
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
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
    // Set TOS low 2 bits (ECN field). Keep upper bits 0.
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