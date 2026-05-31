#ifndef QUIC_COMMON_NETWORK_ADDRESS
#define QUIC_COMMON_NETWORK_ADDRESS

#include <string>
#include <cstdint>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>  // for sockaddr_storage / socklen_t
#endif

namespace quicx {
namespace common {

enum class AddressType {
    kIpv4  = 0x1,
    kIpv6  = 0x2,
};

class Address {
public:
    Address();
    Address(AddressType at);
    Address(const Address& addr);
    Address(const std::string& ip, uint16_t port);
    ~Address();

    virtual void SetIp(const std::string& ip);
    virtual const std::string& GetIp() const;

    virtual void SetPort(uint16_t port);
    virtual uint16_t GetPort() const;

    virtual void SetAddressType(AddressType address_type) { address_type_ = address_type; InvalidateCachedSockaddr(); }
    virtual AddressType GetAddressType() const { return address_type_; }

    virtual const std::string AsString() const;

    friend std::ostream& operator<< (std::ostream &out, Address &addr);
    friend bool operator==(const Address &addr1, const Address &addr2);

    static AddressType CheckAddressType(const std::string& ip);

    // PERF (P1): cached binary form of the address used for sendto(). Filled
    // lazily by SendTo()'s platform implementation; invalidated whenever any
    // setter changes the underlying ip/port/type. The two flavors (`v4` and
    // `v6_mapped`) cover the dual-stack case where the same IPv4 destination
    // can be passed to either an AF_INET socket (use cached_sockaddr_v4_) or
    // an AF_INET6 dual-stack socket (use cached_sockaddr_v6_mapped_).
    //
    // These accessors are intentionally non-virtual and live on Address (not
    // its subclasses) so the SendTo hot path can read/fill them with no
    // virtual dispatch.
    bool HasCachedSockaddr(int family) const;
    const struct sockaddr* GetCachedSockaddr(int family, socklen_t& out_len) const;
    void StoreCachedSockaddr(int family, const struct sockaddr* sa, socklen_t len) const;
    void InvalidateCachedSockaddr() const;

    // PERF (P1 follow-up): proactively populate the sockaddr cache for a
    // given socket family without requiring an actual sendto() call. The
    // SendBatch fast-path probes batch[0]->GetCachedSockaddr() which is
    // empty until SendTo() lazily fills it; but BaseConnection::SendBuffer
    // hands SendBatch a fresh-copied Address per packet, so the lazy fill
    // populated via SendTo() never propagates back to peer_addr_, the
    // source of every copy. Calling this once on peer_addr_ during
    // connection setup ensures every subsequent NetPacket copy carries a
    // ready-to-use cache entry. Cross-platform (uses inet_pton).
    // Returns true if the requested family slot is populated after the
    // call (already-valid is also true).
    bool EnsureSockaddrCache(int family) const;

protected:
    AddressType address_type_;
    std::string ip_;
    uint16_t port_;

private:
    // Cached binary forms. AF_INET → cached_sockaddr_v4_; AF_INET6 (which on
    // an IPv4-typed Address means the IPv4-mapped form ::ffff:a.b.c.d) →
    // cached_sockaddr_v6_. The `mutable` qualifier lets const SendTo paths
    // populate the cache without forcing every call site to drop const.
    mutable struct sockaddr_storage cached_sockaddr_v4_{};
    mutable struct sockaddr_storage cached_sockaddr_v6_{};
    mutable bool cached_v4_valid_ = false;
    mutable bool cached_v6_valid_ = false;
};

}
}

#endif