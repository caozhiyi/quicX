#ifndef QUIC_UDP_NET_PACKET
#define QUIC_UDP_NET_PACKET

#include <memory>
#include "common/network/address.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

class NetPacket {
public:
    NetPacket(): sock_(0), time_(0) {}
    virtual ~NetPacket() {}

    void SetData(std::shared_ptr<common::IBuffer> buffer) { buffer_ = buffer; }
    std::shared_ptr<common::IBuffer> GetData() { return buffer_; }

    void SetAddress(const common::Address& addr) { addr_ = addr; }
    const common::Address& GetAddress() { return addr_; }

    void SetSocket(int32_t sockfd) { sock_ = sockfd; }
    const int32_t GetSocket() { return sock_; }

    void SetTime(uint64_t time) { time_ = time; }
    uint64_t GetTime() { return time_; } 

    void SetEcn(uint8_t ecn) { ecn_ = ecn; }
    uint8_t GetEcn() const { return ecn_; }

protected:
    int32_t sock_; // socket fd
    uint64_t time_; // packet generate time
    common::Address addr_; // peer address
    std::shared_ptr<common::IBuffer> buffer_;
    uint8_t ecn_ {0}; // IP ECN codepoint (2 LSB of IP TOS/TCLASS): 0=Not-ECT, 1=ECT(1), 2=ECT(0), 3=CE
};

}
}

#endif