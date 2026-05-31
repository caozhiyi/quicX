// Cross-platform UDP batch receive primitive.
//
// One implementation, three platforms. The platform difference between
// Linux's recvmmsg(2) and the recvmsg-loop emulation used on macOS /
// Windows is hidden inside common::RecvmMsg() (see network/<os>/io_handle.cpp).
// Everything above this layer — notably src/quic/udp/udp_receiver.cpp —
// only ever sees the unified RecvFromBatch() API and never has to write
// `#ifdef __linux__` again.
//
// What this file does add on top of RecvmMsg:
//   1. Allocates the mmsg / iovec / sockaddr / cmsg scratch arrays on
//      the stack so the hot path does zero heap traffic.
//   2. Wires each entry's caller-owned receive buffer into the iovec.
//   3. After the syscall, parses each mmsg's sockaddr_storage into a
//      `common::Address` (with v4-mapped-in-v6 normalization, matching
//      RecvFromWithEcn's behavior so addressing is consistent across
//      single-pkt and batch paths).
//   4. Optionally walks per-packet ancillary data to extract the ECN
//      codepoint from IP_TOS / IPV6_TCLASS cmsg.
//
// Stack budget: we cap the in-flight batch at kMaxBatch=256. With one
// 128-byte cmsg buffer per datagram plus iovec/mmsghdr/sockaddr arrays
// the worst-case stack frame is around 64 KiB — comfortable on every
// thread we run, including the EventLoop thread.

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#endif

#include "common/network/io_handle.h"

namespace quicx {
namespace common {

namespace {

// Hard cap on per-call batch size. Beyond this point the syscall benefit
// plateaus and the stack frame grows uncomfortably; pick the same value
// as the historical udp_receiver Linux fast path.
constexpr uint32_t kMaxBatch = 256;

// Bytes of cmsg scratch reserved per datagram. IP_TOS / IPV6_TCLASS each
// occupy 16-24 bytes including alignment; 128 bytes is generous and
// keeps us safe if we add IP_PKTINFO / IPV6_PKTINFO later.
constexpr size_t kCmsgPerDgram = 128;

// Translate a sockaddr_storage filled in by recvmmsg/recvmsg into our
// internal Address representation. Mirrors the v4-mapped-in-v6 handling
// used by RecvFromWithEcn so that callers see exactly the same peer
// address format whether they take the single-pkt or batch path.
//
// On platforms where we don't even build this (Windows w/ v4-only
// sockets) the v6 branch is dead code — but cheap and structurally
// safe to leave compiled in.
void FillPeerAddress(const sockaddr_storage& ss, Address& addr) {
    char ipstr[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&ss);
        inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof(ipstr));
        addr.SetIp(ipstr);
        addr.SetPort(ntohs(sin->sin_port));
        addr.SetAddressType(AddressType::kIpv4);
        return;
    }
    if (ss.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(&ss);
#ifdef IN6_IS_ADDR_V4MAPPED
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            in_addr v4;
            std::memcpy(&v4, &sin6->sin6_addr.s6_addr[12], 4);
            inet_ntop(AF_INET, &v4, ipstr, sizeof(ipstr));
            addr.SetIp(ipstr);
            addr.SetPort(ntohs(sin6->sin6_port));
            addr.SetAddressType(AddressType::kIpv4);
            return;
        }
#endif
        inet_ntop(AF_INET6, &sin6->sin6_addr, ipstr, sizeof(ipstr));
        addr.SetIp(ipstr);
        addr.SetPort(ntohs(sin6->sin6_port));
        addr.SetAddressType(AddressType::kIpv6);
    }
}

#ifndef _WIN32
// Walk the ancillary data buffer attached to one received datagram and
// extract the ECN codepoint, if present. Looks for IP_TOS (IPv4) and
// IPV6_TCLASS (IPv6) — these are the values delivered when EnableUdpEcn
// has set IP_RECVTOS / IPV6_RECVTCLASS on the socket.
//
// Returns 0 when no relevant cmsg is found, which matches the "no ECN
// signaling available" semantics expected by the QUIC stack.
uint8_t ParseEcnFromCmsg(msghdr* mh) {
    uint8_t ecn = 0;
    for (cmsghdr* c = CMSG_FIRSTHDR(mh); c != nullptr; c = CMSG_NXTHDR(mh, c)) {
        if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_TOS) {
            // IP_TOS arrives as int on Linux/macOS; copy by memcpy to
            // dodge any alignment trap on strict-alignment platforms.
            int tos = 0;
            std::memcpy(&tos, CMSG_DATA(c), sizeof(tos));
            ecn = static_cast<uint8_t>(tos & 0x03);
        }
#ifdef IPV6_TCLASS
        if (c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_TCLASS) {
            int tclass = 0;
            std::memcpy(&tclass, CMSG_DATA(c), sizeof(tclass));
            ecn = static_cast<uint8_t>(tclass & 0x03);
        }
#endif
    }
    return ecn;
}
#endif  // !_WIN32

// MSG_DONTWAIT does not exist on Windows winsock2; on Windows the
// socket's own non-blocking flag (SocketNoblocking()) is what makes
// the recv calls return WSAEWOULDBLOCK promptly when empty.
#ifdef _WIN32
constexpr int kRecvBatchFlag = 0;
#else
constexpr int kRecvBatchFlag = MSG_DONTWAIT;
#endif

}  // namespace

SysCallInt32Result RecvFromBatch(int32_t sockfd, RecvBatchEntry* entries,
                                 uint32_t entries_count, bool want_ecn) {
    if (entries == nullptr || entries_count == 0) {
        return {0, 0};
    }
    if (entries_count > kMaxBatch) {
        entries_count = kMaxBatch;
    }

    // Stack-allocated scratch arrays. Using fixed-size kMaxBatch (rather
    // than `entries_count`) means a single layout for all call sites and
    // lets the compiler reason about the frame size.
    //
    // We don't default-initialize iovs[] / mmsgs[] / addrs[] here because
    // every used slot (i < entries_count) gets fully overwritten in the
    // wiring loop below, and slots beyond `entries_count` are never read.
    // mmsgs[] is memset-zeroed for the kernel's strict expectation that
    // unused msghdr fields (msg_flags, padding) start at 0.
    MMsghdr           mmsgs[kMaxBatch];
    Iovec             iovs [kMaxBatch];
    sockaddr_storage  addrs[kMaxBatch];
    char              cmsg_pool[kMaxBatch * kCmsgPerDgram];

    std::memset(mmsgs, 0, sizeof(MMsghdr) * entries_count);
    std::memset(addrs, 0, sizeof(sockaddr_storage) * entries_count);

    // Wire one Iovec per entry pointing at the caller-supplied buffer.
    // We never split a datagram across iovecs — UDP semantics mean each
    // receive maps to exactly one buffer.
    for (uint32_t i = 0; i < entries_count; ++i) {
        iovs[i].iov_base_ = entries[i].buf_;
        iovs[i].iov_len_  = entries[i].buf_len_;

        Msghdr& h = mmsgs[i].msg_hdr_;
        h.msg_name_       = &addrs[i];
        h.msg_namelen_    = sizeof(addrs[i]);
        h.msg_iov_        = &iovs[i];
        h.msg_iovlen_     = 1;
        // Always supply a cmsg buffer so that, if the socket happens to
        // be configured to deliver ancillary data, the kernel does not
        // set MSG_CTRUNC. Cost is negligible (one stack memset).
        h.msg_control_    = cmsg_pool + (i * kCmsgPerDgram);
        h.msg_controllen_ = kCmsgPerDgram;
        h.msg_flags_      = 0;

        // Pre-zero the output fields the caller will read on success.
        entries[i].bytes_ = 0;
        entries[i].ecn_   = 0;
    }

    // The platform-specific work — actually pulling datagrams off the
    // socket — is fully encapsulated by RecvmMsg(). On Linux that's
    // recvmmsg(2); on macOS/Windows it's a recvmsg loop with EAGAIN
    // early-exit (see io_handle.cpp on each platform).
    SysCallInt32Result rc = RecvmMsg(sockfd, mmsgs, entries_count,
                                     kRecvBatchFlag, /*time_out=*/0);
    if (rc.return_value_ <= 0) {
        // Either real error or "0 datagrams + 0 errno" (socket empty).
        // Either way, nothing to copy into entries; pass result through.
        return rc;
    }

    const uint32_t got = static_cast<uint32_t>(rc.return_value_);
    for (uint32_t i = 0; i < got; ++i) {
        entries[i].bytes_ = mmsgs[i].msg_len_;
        FillPeerAddress(addrs[i], entries[i].peer_addr_);

#ifndef _WIN32
        // ECN parsing. On Windows we currently do not deliver ECN cmsg
        // (EnableUdpEcn is a no-op there); skip parsing entirely.
        if (want_ecn) {
            // Re-cast our portable Msghdr to the system msghdr; field
            // layout matches by design (see Msghdr definition in
            // io_handle.h). This is the same trick used elsewhere in
            // io_handle.cpp.
            entries[i].ecn_ = ParseEcnFromCmsg(reinterpret_cast<msghdr*>(&mmsgs[i].msg_hdr_));
        }
#else
        (void)want_ecn;
#endif
    }

    return rc;
}

}  // namespace common
}  // namespace quicx
