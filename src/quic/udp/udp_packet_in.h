#ifndef QUIC_UDP_PACKET_IN
#define QUIC_UDP_PACKET_IN

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "common/network/address.h"
#include "quic/packet/packet_interface.h"
#include "quic/udp/udp_packet_interface.h"

namespace quicx {

class IBufferRead;
class UdpPacketIn:
    public IUdpPacket  {
public:
    UdpPacketIn(std::shared_ptr<IBufferRead> buffer);
    ~UdpPacketIn();

    // set recv data from peer
    bool SetData(char* data, uint32_t size);
    // decode data to quic packet
    bool Decode(std::vector<std::shared_ptr<IPacket>>& out_packets);

private:
    std::shared_ptr<IBufferRead> _recv_buffer;
};

}

#endif