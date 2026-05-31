#include <cstring>
#include <ostream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include "common/network/address.h"

namespace quicx {
namespace common {

Address::Address():
    address_type_(AddressType::kIpv4),
    port_(0) {

}

Address::Address(AddressType at):
    address_type_(at),
    port_(0) {
    
}

Address::Address(const Address& addr):
    address_type_(addr.address_type_),
    ip_(addr.ip_),
    port_(addr.port_) {
    // PERF (P1 follow-up): copy the sendto sockaddr cache too. Earlier this
    // ctor deliberately dropped the cache "in case the copy is used on a
    // different fd/family" — but the cache is keyed by family in two
    // independent slots (v4/v6) and any setter that mutates ip/port/type
    // already calls InvalidateCachedSockaddr(), so re-using a populated
    // cache after a copy is always safe.
    //
    // Why this matters: BaseConnection::SendBuffer creates a fresh
    // NetPacket per packet and assigns NetPacket->SetAddress(
    // AcquireSendAddress()) where AcquireSendAddress returns a value-typed
    // Address. The connection's peer_addr_ accumulates a populated v4/v6
    // cache after the first sendto, but every NetPacket got a copy with
    // an empty cache, so UdpSender::SendBatch's fast-path probe (which
    // tests batch[0]->GetAddress().GetCachedSockaddr(...)) saw a miss on
    // every batch and fell back to per-packet Send(). Net effect: the
    // entire sendmmsg(2) optimisation was a no-op on the file_transfer
    // hot path; observed udp_sb_ok=0 across all 17s of a 500MB upload.
    if (addr.cached_v4_valid_) {
        std::memcpy(&cached_sockaddr_v4_, &addr.cached_sockaddr_v4_, sizeof(cached_sockaddr_v4_));
        cached_v4_valid_ = true;
    }
    if (addr.cached_v6_valid_) {
        std::memcpy(&cached_sockaddr_v6_, &addr.cached_sockaddr_v6_, sizeof(cached_sockaddr_v6_));
        cached_v6_valid_ = true;
    }
}

Address::Address(const std::string& ip, uint16_t port):
    ip_(ip),
    port_(port) {
    address_type_ = CheckAddressType(ip);
}

Address::~Address() {

}

void Address::SetIp(const std::string& ip) {
    ip_ = ip;
    InvalidateCachedSockaddr();
}

const std::string& Address::GetIp() const {
    return ip_;
}

void Address::SetPort(uint16_t port) {
    port_ = port;
    InvalidateCachedSockaddr();
}

uint16_t Address::GetPort() const {
    return port_;
}

const std::string Address::AsString() const {
    return std::move(ip_ + ":" + std::to_string(port_));
}

std::ostream& operator<< (std::ostream &out, Address &addr) {
    const std::string str = addr.AsString();
    out.write(str.c_str(), str.length());
    return out;
}

bool operator==(const Address &addr1, const Address &addr2) {
    return addr1.ip_ == addr2.ip_ && addr1.port_ == addr2.port_;
}

AddressType Address::CheckAddressType(const std::string& ip) {
    if (ip.find(':') == std::string::npos) {
        return AddressType::kIpv4;
    }
    
    return AddressType::kIpv6;
}

bool Address::HasCachedSockaddr(int family) const {
    if (family == AF_INET) return cached_v4_valid_;
    if (family == AF_INET6) return cached_v6_valid_;
    return false;
}

const struct sockaddr* Address::GetCachedSockaddr(int family, socklen_t& out_len) const {
    if (family == AF_INET && cached_v4_valid_) {
        out_len = sizeof(struct sockaddr_in);
        return reinterpret_cast<const struct sockaddr*>(&cached_sockaddr_v4_);
    }
    if (family == AF_INET6 && cached_v6_valid_) {
        out_len = sizeof(struct sockaddr_in6);
        return reinterpret_cast<const struct sockaddr*>(&cached_sockaddr_v6_);
    }
    out_len = 0;
    return nullptr;
}

void Address::StoreCachedSockaddr(int family, const struct sockaddr* sa, socklen_t len) const {
    if (!sa) return;
    if (family == AF_INET && len <= static_cast<socklen_t>(sizeof(cached_sockaddr_v4_))) {
        memcpy(&cached_sockaddr_v4_, sa, len);
        cached_v4_valid_ = true;
    } else if (family == AF_INET6 && len <= static_cast<socklen_t>(sizeof(cached_sockaddr_v6_))) {
        memcpy(&cached_sockaddr_v6_, sa, len);
        cached_v6_valid_ = true;
    }
}

void Address::InvalidateCachedSockaddr() const {
    cached_v4_valid_ = false;
    cached_v6_valid_ = false;
}

bool Address::EnsureSockaddrCache(int family) const {
    // Idempotent: a populated slot is left untouched.
    if (family == AF_INET && cached_v4_valid_) {
        return true;
    }
    if (family == AF_INET6 && cached_v6_valid_) {
        return true;
    }

    if (family == AF_INET) {
        struct sockaddr_in sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port_);
        // inet_pton returns 1 on success. Anything else (0 == not parseable
        // for family, -1 == EAFNOSUPPORT) → leave the slot empty so the
        // caller falls back to the legacy lazy-parse path inside SendTo().
        if (inet_pton(AF_INET, ip_.c_str(), &sa.sin_addr) != 1) {
            return false;
        }
        std::memcpy(&cached_sockaddr_v4_, &sa, sizeof(sa));
        cached_v4_valid_ = true;
        return true;
    }

    if (family == AF_INET6) {
        struct sockaddr_in6 sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(port_);
        // For an IPv4-typed Address used on an IPv6 (dual-stack) socket,
        // mirror the SendTo() platform paths and store the v4-mapped form.
        if (address_type_ == AddressType::kIpv4 && ip_.find(':') == std::string::npos) {
            std::string mapped = "::ffff:" + ip_;
            if (inet_pton(AF_INET6, mapped.c_str(), &sa.sin6_addr) != 1) {
                return false;
            }
        } else {
            if (inet_pton(AF_INET6, ip_.c_str(), &sa.sin6_addr) != 1) {
                return false;
            }
        }
        std::memcpy(&cached_sockaddr_v6_, &sa, sizeof(sa));
        cached_v6_valid_ = true;
        return true;
    }
    return false;
}

}
}