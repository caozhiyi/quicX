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
    Iovec() : iov_base_(nullptr), iov_len_(0) {}
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

// Result returned by UDP socket creation. Carries the underlying address
// family alongside the fd so callers (and io_handle internals) never need
// to probe the socket via getsockopt/SO_DOMAIN/IPV6_V6ONLY at runtime.
//
// Field layout intentionally mirrors SysCallInt32Result so existing code
// that only reads `return_value_` / `error_code_` keeps compiling unchanged.
struct UdpSocketResult {
    int32_t return_value_;   // socket fd (>=0 on success, -1 on failure)
    int32_t error_code_;     // 0 on success, errno otherwise
    int32_t family_;         // AF_INET or AF_INET6 on success, 0 on failure
};

// Create a dual-stack UDP socket (AF_INET6 with IPV6_V6ONLY=0). The
// returned `family_` will be AF_INET6 in the normal case, or AF_INET if
// the platform does not support IPv6 and we fell back to a v4-only socket.
UdpSocketResult UdpSocket();

// Create an IPv4-only UDP socket (AF_INET). `family_` is always AF_INET.
UdpSocketResult UdpSocket4();

SysCallInt32Result Close(int32_t sockfd);

// Bind a socket to an address.
// Prefer the overload that takes an explicit `sock_family` (AF_INET /
// AF_INET6) — passing the family tells Bind() exactly how to format the
// sockaddr without having to inspect the socket via getsockname/SO_DOMAIN.
//
// The single-argument overload is kept for callers that don't track the
// family (notably the TCP/upgrade path); it falls back to internal
// detection (cache lookup -> getsockname -> getsockopt).
SysCallInt32Result Bind(int32_t sockfd, int32_t sock_family, Address& addr);
SysCallInt32Result Bind(int32_t sockfd, Address& addr);
SysCallInt32Result Accept(int32_t sockfd, Address& addr);
SysCallInt32Result Listen(int32_t sockfd, int32_t backlog);

SysCallInt32Result Write(int32_t sockfd, const char *data, uint32_t len);
SysCallInt32Result Writev(int32_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result SendTo(int32_t sockfd, const char *msg, uint32_t len, uint16_t flag, const Address& addr);
SysCallInt32Result SendMsg(int32_t sockfd, const Msghdr* msg, int16_t flag);
SysCallInt32Result SendmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag);

// PERF: UDP Generic Segmentation Offload (Linux UDP_SEGMENT cmsg, kernel 4.18+).
//
// Sends a single contiguous payload of `total_len` bytes to `addr`, asking the
// kernel to slice it into datagrams of `segment_size` bytes each (the last
// segment may be shorter). On a GSO-capable path (loopback, most modern NICs)
// this collapses N sendmsg/sendmmsg calls + N protocol-stack traversals into
// 1 syscall + 1 traversal, which is the largest single CPU win available for
// QUIC-style "many same-sized packets to the same peer" traffic.
//
// Preconditions enforced by the caller (UdpSender::SendBatch):
//   - all packets in the run share the same destination Address;
//   - all packets except optionally the last share the same length
//     (`segment_size`);
//   - run length <= 64 (Linux UDP_MAX_SEGMENTS hard cap; older kernels
//     enforce this even when sysctl_max_segs is higher);
//   - total_len <= 65535 - sizeof(udphdr) (UDP datagram size limit).
//
// Error mapping:
//   - return_value_ = number of bytes the kernel queued (>=0 on success);
//     on success this equals `total_len`.
//   - error_code_ = 0 on success.
//   - error_code_ = EIO when GSO is statically unsupported (lib stub /
//     kernel < 4.18). Caller should permanently disable GSO and fall
//     back to sendmmsg.
//   - error_code_ = EINVAL / ENOTSUP / ENOPROTOOPT signal "this socket /
//     path does not accept UDP_SEGMENT" — caller should also disable.
//   - other errno values are real send failures, propagate as today.
//
// Not implemented (returns EIO) on macOS — caller must check error_code_.
SysCallInt32Result SendMsgGso(int32_t sockfd,
                              const char* payload, uint32_t total_len,
                              uint16_t segment_size,
                              const Address& addr);

SysCallInt32Result Recv(int32_t sockfd, char *data, uint32_t len, uint16_t flag);
SysCallInt32Result Readv(int32_t sockfd, Iovec *vec, uint32_t vec_len);
SysCallInt32Result RecvFrom(int32_t sockfd, char *msg, uint32_t len, uint16_t flag, Address& addr);
SysCallInt32Result RecvMsg(int32_t sockfd, Msghdr* msg, int16_t flag);
SysCallInt32Result RecvmMsg(int32_t sockfd, MMsghdr* msgvec, uint32_t vlen, uint16_t flag, uint32_t time_out);

SysCallInt32Result SetSockOpt(int32_t sockfd, int level, int optname, const void *optval, uint32_t optlen);

// Default UDP socket buffer size requested for every QUIC UDP socket.
// 4 MiB is the "good middle ground" used by Chromium/QUICHE example servers
// and is plenty for hundreds of concurrent QUIC connections at LAN/loopback
// rates. quic-go documents up to 7.5 MB for very high-bandwidth WAN paths;
// applications may call SetUdpSocketBuffer() again with a larger value.
//
// NOTE: Linux clamps this to `min(2*requested, net.core.{r,w}mem_max)`. So
// on a stock Linux box (rmem_max=212992 by default) the kernel will silently
// truncate to ~208 KiB and SetUdpSocketBuffer() will print a one-time
// warning suggesting `sysctl -w net.core.rmem_max=...`.
constexpr int32_t kDefaultUdpBufferSize = 4 * 1024 * 1024;  // 4 MiB

// Try to enlarge a UDP socket's SO_RCVBUF and SO_SNDBUF to `size_bytes`.
// Reads back the actual values and emits a one-time LOG_WARN if either
// kernel-applied size is < 70% of `size_bytes` (typically because
// net.core.{r,w}mem_max is too small). Always returns success — buffer
// sizing is a soft optimization, never a hard error.
//
// This is the same playbook quic-go / msquic / QUICHE follow: ask for a
// large buffer up front so kernel UDP receive queues don't drop packets
// under burst load (handshake storms, hundreds of concurrent clients,
// high-bandwidth bulk transfer). Without this, on a stock Linux box
// 100+ concurrent QUIC handshakes overflow the default 224 KiB receive
// queue and packets are silently dropped — the connection then waits
// for PTO retransmits, manifesting as long-tail stalls in load tests.
SysCallInt32Result SetUdpSocketBuffer(int32_t sockfd, int32_t size_bytes);

SysCallInt32Result SocketNoblocking(int32_t sockfd);

bool ParseRemoteAddress(uint16_t fd, Address& addr);

bool ParseLocalAddress(int32_t fd, Address& addr);

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

// Per-datagram entry passed to RecvFromBatch.
//   buf_ / buf_len_ : in.  caller-owned receive buffer (one per datagram).
//   bytes_          : out. number of bytes actually received into buf_.
//   peer_addr_      : out. sender address (v4-mapped-in-v6 normalized to v4).
//   ecn_            : out. ECN codepoint (0..3); 0 if want_ecn=false or
//                          the platform does not deliver ECN cmsg.
//
// Plain-old-data layout so the UDP receiver can stack-allocate an array
// of these without heap traffic on the hot path.
struct RecvBatchEntry {
    char*    buf_;
    uint32_t buf_len_;
    uint32_t bytes_;
    Address  peer_addr_;
    uint8_t  ecn_;
};

// Drain up to `entries_count` UDP datagrams from `sockfd` non-blockingly
// into `entries`, optionally extracting per-packet ECN codepoints.
//
// This is the cross-platform batch receive primitive used by the UDP
// receiver. Implementation lives in common/network/recv_batch.cpp and
// internally uses the existing RecvmMsg() abstraction:
//   - on Linux it bottoms out in a single recvmmsg(MSG_DONTWAIT) syscall;
//   - on macOS / Windows it bottoms out in a recvmsg / WSARecvMsg loop
//     that exits early on EAGAIN / EWOULDBLOCK.
// Either way the caller sees a single "drain N datagrams" operation
// with no platform branching above this layer.
//
// Result semantics:
//   return_value_  : number of datagrams written to entries (>= 0).
//                    A return of 0 with error_code_=0 means "socket
//                    momentarily empty" — the caller should simply wait
//                    for the next read event.
//   error_code_    : 0 on success (including the EAGAIN-after-N case);
//                    otherwise the underlying errno (e.g. EBADF) and
//                    return_value_ contains whatever datagrams had
//                    already been delivered before the error so the
//                    caller can still process them.
//
// Pre-conditions: each entries[i].buf_ must point to a writable buffer
// of entries[i].buf_len_ bytes; entries_count >= 1.
SysCallInt32Result RecvFromBatch(int32_t sockfd, RecvBatchEntry* entries,
                                 uint32_t entries_count, bool want_ecn);


}
}

#endif