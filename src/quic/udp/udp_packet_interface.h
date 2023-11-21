#ifndef QUIC_UDP_IPACKET_INTERFACE
#define QUIC_UDP_IPACKET_INTERFACE

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "common/network/address.h"
#include "quic/packet/packet_interface.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {
namespace quic {

class IUdpPacket {
public:
    IUdpPacket() {}
    virtual ~IUdpPacket() {}

    void SetData(std::shared_ptr<common::IBuffer> buffer) { _buffer = buffer; }
    std::shared_ptr<common::IBuffer> GetData() const { return _buffer; }

protected:
    std::shared_ptr<common::IBuffer> _buffer;
};

}
}

#endif