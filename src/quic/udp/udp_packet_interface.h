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

class IUdpPacket {
public:
    IUdpPacket();
    virtual ~IUdpPacket();

    void SetPeerAddress(const Address&& addr); 
    const Address& GetPeerAddress() const;

    void SetData(const std::shared_ptr<IBuffer>&& buffer);
    std::shared_ptr<IBuffer> GetBuffer() const;

private:
    Address _peer_addr;
    std::shared_ptr<IBuffer> _buffer;
};

}

#endif