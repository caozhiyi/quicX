#ifdef _WIN32
#include <mswsock.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "common/network/io_handle.h"

namespace quicx {
namespace common {

SysCallInt64Result UdpSocket() {
    int64_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return {sock, sock != INVALID_SOCKET ? 0 : WSAGetLastError()};
}

SysCallInt32Result Close(int64_t sockfd) {
    const int32_t rc = closesocket(sockfd);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Bind(int64_t sockfd, Address& addr) {
    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(addr.GetPort());
    inet_pton(AF_INET, addr.GetIp().c_str(), &addr_in.sin_addr);

    const int32_t rc = bind(sockfd, (sockaddr*)&addr_in, sizeof(addr_in));
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Write(int64_t sockfd, const char *data, uint32_t len) {
    const int32_t rc = send(sockfd, data, len, 0);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Writev(int64_t sockfd, Iovec *vec, uint32_t vec_len) {
    DWORD bytes_sent;
    const int32_t rc = WSASend(sockfd, (WSABUF*)vec, vec_len, &bytes_sent, 0, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SendTo(int64_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr) {
    struct sockaddr_in addr_cli;
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(addr.GetPort());
    inet_pton(AF_INET, addr.GetIp().c_str(), &addr_cli.sin_addr);

    const int32_t rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_cli, sizeof(addr_cli));
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SendMsg(int64_t sockfd, const Msghdr* msg, int16_t flag) {
    DWORD bytes_sent;
    const int32_t rc = WSASendMsg(sockfd, (LPWSAMSG)msg, flag, &bytes_sent, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SendmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag) {
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

SysCallInt32Result Recv(int64_t sockfd, char *data, uint32_t len, uint16_t flag) {
    const int32_t rc = recv(sockfd, data, len, flag);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result Readv(int64_t sockfd, Iovec *vec, uint32_t vec_len) {
    DWORD bytes_received;
    const int32_t rc = WSARecv(sockfd, (WSABUF*)vec, vec_len, &bytes_received, NULL, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result RecvFrom(int64_t sockfd, char *msg, uint32_t len, uint16_t flag, Address& addr) {
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

SysCallInt32Result RecvMsg(int64_t sockfd, Msghdr* msg, int16_t flag) {
    DWORD bytes_received;
    const int32_t rc = WSARecvMsg(sockfd, (LPWSAMSG)msg, &bytes_received, NULL, NULL);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result RecvmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out) {
    DWORD bytes_received;
    int32_t rc = 0;
    for (uint32_t i = 0; i < vlen; ++i) {
        rc = WSARecvMsg(sockfd, (LPWSAMSG)&msgvec[i].msg_hdr_, &bytes_received, NULL, NULL);
        if (rc == SOCKET_ERROR) {
            return {rc, WSAGetLastError()};
        }
        msgvec[i].msg_len_ = bytes_received;
    }
    return {rc, 0};
}

SysCallInt32Result SetSockOpt(int64_t sockfd, int level, int optname, const void *optval, uint32_t optlen) {
    const int32_t rc = setsockopt(sockfd, level, optname, (const char*)optval, optlen);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
}

SysCallInt32Result SocketNoblocking(uint64_t sock) {
    u_long mode = 1;
    const int32_t rc = ioctlsocket(sock, FIONBIO, &mode);
    return {rc, rc != SOCKET_ERROR ? 0 : WSAGetLastError()};
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

    struct sockaddr_in *addr_in = (struct sockaddr_in *)res->ai_addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ip, sizeof(ip));
    addr.SetIp(ip);
    addr.SetPort(ntohs(addr_in->sin_port));

    freeaddrinfo(res);
    return true;
}

}
}

#endif
