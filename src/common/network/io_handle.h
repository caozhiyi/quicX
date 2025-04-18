#ifndef QUIC_COMMON_NETWORK_IO_HANDLE
#define QUIC_COMMON_NETWORK_IO_HANDLE

#include <cstdint>
#include "common/util/os_return.h"
#include "common/network/address.h"

namespace quicx {
namespace common {

struct Iovec {
    void      *iov_base_;      // starting address of buffer
    size_t    iov_len_;        // size of buffer
    Iovec(void* base, size_t len) : iov_base_(base), iov_len_(len) {}
};

struct Msghdr {
    void *msg_name_;		/* Address to send to/receive from.  */
    uint32_t msg_namelen_;	/* Length of address data.  */

    struct Iovec *msg_iov_;	/* Vector of data to send/receive into.  */
    size_t msg_iovlen_;		/* Number of elements in the vector.  */

    void *msg_control_;		/* Ancillary data (eg BSD filedesc passing). */
    size_t msg_controllen_;	/* Ancillary data buffer length.*/

    int16_t msg_flags_;		/* Flags on received message.  */
};

struct MMsghdr {
    Msghdr   msg_hdr_;		/* Actual message header.  */
    uint32_t msg_len_;	/* Number of received or sent bytes for the entry.  */
};

SysCallInt64Result UdpSocket();

SysCallInt32Result Close(int64_t sockfd);

SysCallInt32Result Bind(int64_t sockfd, Address& addr);
SysCallInt64Result Accept(int64_t sockfd, Address& addr);

SysCallInt32Result Write(int64_t sockfd, const char *data, uint32_t len);
SysCallInt32Result Writev(int64_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result SendTo(int64_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr);
SysCallInt32Result SendMsg(int64_t sockfd, const Msghdr* msg, int16_t flag);
SysCallInt32Result SendmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag);

SysCallInt32Result Recv(int64_t sockfd, char *data, uint32_t len, uint16_t flag);
SysCallInt32Result Readv(int64_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result RecvFrom(int64_t sockfd, char *msg, uint32_t len, uint16_t flag, Address& addr);
SysCallInt32Result RecvMsg(int64_t sockfd, Msghdr* msg, int16_t flag);
SysCallInt32Result RecvmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out);

SysCallInt32Result SetSockOpt(int64_t sockfd, int level, int optname, const void *optval, uint32_t optlen);

SysCallInt32Result SocketNoblocking(uint64_t sock);

bool LookupAddress(const std::string& host, Address& addr);

}
}

#endif