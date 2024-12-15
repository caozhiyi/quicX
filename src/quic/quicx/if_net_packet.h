#ifndef QUIC_QUICX_IF_NET_PACKET
#define QUIC_QUICX_IF_NET_PACKET

#include <memory>
#include "common/network/address.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

class INetPacket {
public:
    INetPacket() {}
    virtual ~INetPacket() {}

    void SetData(std::shared_ptr<common::IBuffer> buffer) { buffer_ = buffer; }
    std::shared_ptr<common::IBuffer> GetData() { return buffer_; }

    void SetAddress(const common::Address& addr) { addr_ = addr; }
    const common::Address& GetAddress() { return addr_; }

    void SetSocket(uint64_t sock) { sock_ = sock; }
    const uint64_t GetSocket() { return sock_; }

    void SetTime(uint64_t time) { time_ = time; }
    uint64_t GetTime() { return time_; } 

protected:
    uint64_t sock_; // socket fd
    uint64_t time_; // packet generate time
    common::Address addr_; // peer address
    std::shared_ptr<common::IBuffer> buffer_;
};

}
}

#endif