#include <unistd.h>       // for close
#include <ifaddrs.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include "io_handle.h"

namespace quicx {

SysCallInt64Result UdpSocket() {
    int64_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return {sock, sock != -1 ? 0 : errno};
}

SysCallInt32Result Close(int64_t sockfd) {
    const int rc = close(sockfd);
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result Bind(int64_t sockfd, Address& addr) {
    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(addr.GetPort());
    addr_in.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());

    const int rc = bind(sockfd, (sockaddr*)&addr_in, sizeof(addr_in));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result SendTo(int64_t sockfd, const char *msg, uint32_t len, uint16_t flag, Address& addr) {
    struct sockaddr_in addr_cli;
    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(addr.GetPort());
    addr_cli.sin_addr.s_addr = inet_addr(addr.GetIp().c_str());

    const int rc = sendto(sockfd, msg, len, flag, (sockaddr*)&addr_cli, sizeof(addr_cli));
    return {rc, rc != -1 ? 0 : errno};
}

SysCallInt32Result RecvFrom(int64_t sockfd, char *buf, uint32_t len, uint16_t flag, Address& addr) {
    struct sockaddr_in addr_cli;
    socklen_t fromlen = sizeof(sockaddr);

    const int rc = recvfrom(sockfd, buf, len, 0, (sockaddr*)&addr_cli, &fromlen);
    if (rc == -1) {
        return {rc, errno};
    }
    
    addr.SetIp(inet_ntoa(addr_cli.sin_addr));
    addr.SetPort(ntohs(addr_cli.sin_port));
    return {rc, 0};
}

}
