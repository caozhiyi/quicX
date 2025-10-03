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


SysCallInt32Result TcpSocket();

SysCallInt32Result UdpSocket();

SysCallInt32Result Close(int32_t sockfd);

SysCallInt32Result Bind(int32_t sockfd, Address& addr);
SysCallInt32Result Accept(int32_t sockfd, Address& addr);
SysCallInt32Result Listen(int32_t sockfd, int32_t backlog);

SysCallInt32Result Write(int32_t sockfd, const char *data, uint32_t len);
SysCallInt32Result Writev(int32_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result SendTo(int32_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr);
SysCallInt32Result SendMsg(int32_t sockfd, const Msghdr* msg, int16_t flag);
SysCallInt32Result SendmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag);

SysCallInt32Result Recv(int32_t sockfd, char *data, uint32_t len, uint16_t flag);
SysCallInt32Result Readv(int32_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result RecvFrom(int32_t sockfd, char *msg, uint32_t len, uint16_t flag, Address& addr);
SysCallInt32Result RecvMsg(int32_t sockfd, Msghdr* msg, int16_t flag);
SysCallInt32Result RecvmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out);

SysCallInt32Result SetSockOpt(int32_t sockfd, int level, int optname, const void *optval, uint32_t optlen);

SysCallInt32Result SocketNoblocking(int32_t sockfd);

bool ParseRemoteAddress(uint16_t fd, Address& addr);

bool LookupAddress(const std::string& host, Address& addr);

bool Pipe(int32_t& pipe1, int32_t& pipe2);

// UDP ECN helpers
// Enable receiving ECN/TOS (IPv4) and Traffic Class (IPv6) on the socket for ECN extraction
SysCallInt32Result EnableUdpEcn(int32_t sockfd);
// Receive a datagram and extract ECN codepoint from ancillary data if available
// ECN values: 0b00 Not-ECT, 0b10 ECT(0), 0b01 ECT(1), 0b11 CE
SysCallInt32Result RecvFromWithEcn(int32_t sockfd, char *buf, uint32_t len, uint16_t flag, Address& addr, uint8_t& ecn);

// Set default ECN marking on outgoing UDP packets (via IP_TOS/IPV6_TCLASS)
// ecn_codepoint: 0x00 Not-ECT, 0x01 ECT(1), 0x02 ECT(0), 0x03 CE (not recommended)
SysCallInt32Result EnableUdpEcnMarking(int32_t sockfd, uint8_t ecn_codepoint);


}
}

#endif