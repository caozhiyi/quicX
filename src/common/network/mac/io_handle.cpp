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
#include "common/network/io_handle.h"

namespace quicx {
namespace common {

SysCallInt64Result UdpSocket() {
    int64_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return {sock, sock != -1 ? 0 : errno};
}

SysCallInt32Result Close(int64_t sockfd) {
    const int32_t rc = close(sockfd);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Bind(int64_t sockfd, Address& addr) {
    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(addr.GetPort());
    addr_in.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());

    const int32_t rc = bind(sockfd, (sockaddr*)&addr_in, sizeof(addr_in));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Write(int64_t sockfd, const char *data, uint32_t len) {
    const int32_t rc = write(sockfd, data, len);
    return {rc, rc != -1 ? 0 : errno};
}
SysCallInt32Result Writev(int64_t sockfd, Iovec *vec, uint32_t vec_len) {
    const int32_t rc = writev(sockfd, (iovec*)vec, vec_len);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendTo(int64_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr) {
    struct sockaddr_in addr_cli;
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(addr.GetPort());
    addr_cli.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());

    const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_cli, sizeof(addr_cli));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendMsg(int64_t sockfd, const Msghdr* msg, int16_t flag) {
    const int32_t rc = sendmsg(sockfd, (msghdr*)msg, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag) {
    return {0, 0};
}

SysCallInt32Result Recv(int64_t sockfd, char *data, uint32_t len, uint16_t flag) {
    const int32_t rc = recv(sockfd, data, len, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Readv(int64_t sockfd, Iovec *vec, uint32_t vec_len) {
    const int32_t rc = readv(sockfd, (iovec*)vec, vec_len);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result RecvFrom(int64_t sockfd, char *buf, uint32_t len, uint16_t flag, Address& addr) {
    struct sockaddr_in addr_cli;
    socklen_t fromlen = sizeof(sockaddr);

    const int32_t rc = recvfrom(sockfd, buf, len, 0, (sockaddr*)&addr_cli, &fromlen);
    if (rc == -1) {
        return {rc, errno};
    }
    
    addr.SetIp(inet_ntoa(addr_cli.sin_addr));
    addr.SetPort(ntohs(addr_cli.sin_port));
    return {rc, 0};
}

SysCallInt32Result RecvMsg(int64_t sockfd, Msghdr* msg, int16_t flag) {
    const int32_t rc = recvmsg(sockfd, (msghdr*)msg, flag);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result RecvmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out) {
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

SysCallInt32Result SetSockOpt(int64_t sockfd, int level, int optname, const void *optval, uint32_t optlen) {
    const int32_t rc = setsockopt(sockfd, level, optname, optval, optlen);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SocketNoblocking(uint64_t sock) {
    int32_t old_option = fcntl(sock, F_GETFL);
    int32_t new_option = old_option | O_NONBLOCK;
    const int32_t rc = fcntl(sock, F_SETFL, new_option);
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
            addr.SetAddressType(AddressType::AT_IPV4);
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr_ptr = &(ipv6->sin6_addr);
            addr.SetAddressType(AddressType::AT_IPV6);
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

}
}

#endif