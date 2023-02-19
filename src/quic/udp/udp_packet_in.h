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

private:
    std::shared_ptr<IBufferRead> _recv_buffer;
};

}

#endif