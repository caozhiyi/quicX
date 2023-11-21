#ifndef QUIC_COMMON_NETWORK_IO_HANDLE
#define QUIC_COMMON_NETWORK_IO_HANDLE

#include <cstdint>
#include "address.h"
#include "common/util/os_return.h"

namespace quicx {
namespace common {

struct Iovec {
    void      *_iov_base;      // starting address of buffer
    size_t    _iov_len;        // size of buffer
    Iovec(void* base, size_t len) : _iov_base(base), _iov_len(len) {}
};

struct Msghdr {
    void *_msg_name;		/* Address to send to/receive from.  */
    uint32_t _msg_namelen;	/* Length of address data.  */

    struct Iovec *_msg_iov;	/* Vector of data to send/receive into.  */
    size_t _msg_iovlen;		/* Number of elements in the vector.  */

    void *_msg_control;		/* Ancillary data (eg BSD filedesc passing). */
    size_t _msg_controllen;	/* Ancillary data buffer length.*/

    int16_t _msg_flags;		/* Flags on received message.  */
};

struct MMsghdr {
    Msghdr   _msg_hdr;		/* Actual message header.  */
    uint32_t _msg_len;	/* Number of received or sent bytes for the entry.  */
};

SysCallInt64Result UdpSocket();

SysCallInt32Result Close(int64_t sockfd);

SysCallInt32Result Bind(int64_t sockfd, Address& addr);

SysCallInt32Result Write(int64_t sockfd, const char *data, uint32_t len);
SysCallInt32Result Writev(int64_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result SendTo(int64_t sockfd, const char *msg, uint32_t len, uint16_t flag, Address& addr);
SysCallInt32Result SendMsg(int64_t sockfd, const Msghdr* msg, int16_t flag);
SysCallInt32Result SendmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag);

SysCallInt32Result Recv(int64_t sockfd, char *data, uint32_t len, uint16_t flag);
SysCallInt32Result Readv(int64_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result RecvFrom(int64_t sockfd, char *msg, uint32_t len, uint16_t flag, Address& addr);
SysCallInt32Result RecvMsg(int64_t sockfd, Msghdr* msg, int16_t flag);
SysCallInt32Result RecvmMsg(int64_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out);

}
}

#endif