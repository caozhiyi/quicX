#ifndef QUIC_UDP_PACKET_OUT
#define QUIC_UDP_PACKET_OUT

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "common/network/address.h"
#include "quic/packet/if_packet.h"
#include "quic/udp/udp_packet_interface.h"

namespace quicx {
namespace quic {

class UdpPacketOut:
    public IUdpPacket {
public:
    UdpPacketOut() {}
    ~UdpPacketOut() {}

    void SetOutsocket(uint64_t sock) { _out_socket = sock; }
    uint64_t GetOutSocket() { return _out_socket; }

    void SetPeerAddress(common::Address* addr) { _address = addr; }
    common::Address* GetPeerAddress() { return _address; }
private:
    uint64_t _out_socket;
    common::Address* _address;
};

}
}

#endif