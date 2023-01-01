#ifndef QUIC_UDP_PACKET_OUT
#define QUIC_UDP_PACKET_OUT

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "common/network/address.h"
#include "quic/packet/packet_interface.h"
#include "quic/udp/udp_packet_interface.h"

namespace quicx {

class UdpPacketOut:
    public IUdpPacket {
public:
    UdpPacketOut(std::shared_ptr<IBufferWrite> buffer);
    ~UdpPacketOut();

    // set recv data from peer
    bool AddQuicPacket(std::shared_ptr<IPacket> packet);
    // decode data to quic packet
    bool Eecode(char*& out_data, uint32_t& out_len);

private:
    std::shared_ptr<IBufferWrite> _send_buffer;
    std::vector<std::shared_ptr<IPacket>> _packets;
};

}

#endif