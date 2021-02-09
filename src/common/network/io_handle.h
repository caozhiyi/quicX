#ifndef QUIC_COMMON_NETWORK_IO_HANDLE
#define QUIC_COMMON_NETWORK_IO_HANDLE

#include <cstdint>
#include "address.h"
#include "util/os_return.h"


namespace quicx {

SysCallIntResult Bind(int64_t sockfd, const Address& addr);

SysCallIntResult SetSockopt(int64_t sockfd, in level, int optname, const void* optval,
                              socklen_t optlen) override;
SysCallIntResult GetSockopt(int64_t sockfd, int level, int optname, void* optval,
                              socklen_t* optlen) override;

}

#endif