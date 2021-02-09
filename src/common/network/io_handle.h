#ifndef QUIC_COMMON_NETWORK_IO_HANDLE
#define QUIC_COMMON_NETWORK_IO_HANDLE

#include <cstdint>
#include "address.h"
#include "util/os_return.h"

namespace quicx {

SysCallInt64Result UdpSocket();

SysCallInt32Result Close(int64_t sockfd);

SysCallInt32Result Bind(int64_t sockfd, Address& addr);

SysCallInt32Result SendTo(int64_t sockfd, const char *msg, uint32_t len, uint16_t flag, Address& addr);

SysCallInt32Result RecvFrom(int64_t sockfd, char *buf, uint32_t len, uint16_t flag, Address& addr);

}

#endif