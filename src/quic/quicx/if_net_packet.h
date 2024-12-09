#ifndef QUIC_QUICX_IF_NET_PACKET
#define QUIC_QUICX_IF_NET_PACKET

#include <memory>
#include "common/network/address.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {
namespace quic {

class INetPacket {
public:
    INetPacket() {}
    virtual ~INetPacket() {}

    void SetData(std::shared_ptr<common::IBuffer> buffer) { _buffer = buffer; }
    std::shared_ptr<common::IBuffer> GetData() { return _buffer; }

    void SetAddress(const common::Address& addr) { _addr = addr; }
    const common::Address& GetAddress() { return _addr; }

    void SetSocket(uint64_t sock) { _sock = sock; }
    const uint64_t GetSocket() { return _sock; }

    void SetTime(uint64_t time) { _time = time; }
    uint64_t GetTime() { return _time; } 

protected:
    uint64_t _sock; // socket fd
    uint64_t _time; // packet generate time
    common::Address _addr; // peer address
    std::shared_ptr<common::IBuffer> _buffer;
};

}
}

#endif