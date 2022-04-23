#ifndef QUIC_UDP_IPACKET_INTERFACE
#define QUIC_UDP_IPACKET_INTERFACE

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "common/network/address.h"
#include "quic/packet/packet_interface.h"
#include "common/buffer/buffer_writeonly.h"

namespace quicx {

class IUdpPacket {
public:
    IUdpPacket();
    ~IUdpPacket();

    // peer address
    void SetPeerAddress(std::shared_ptr<Address> peer_addr);
    std::shared_ptr<Address> GetPeerAddress();

private:
    std::shared_ptr<Address> _peer_addr;
};

}

#endif