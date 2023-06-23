#ifndef QUIC_PROCESS_CLIENT_PROCESSOR
#define QUIC_PROCESS_CLIENT_PROCESSOR

#include "quic/udp/udp_packet_in.h"
#include "quic/process/processor_interface.h"

namespace quicx {

class ClientProcessor:
    public IProcessor {
public:
    ClientProcessor();
    virtual ~ClientProcessor();

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet);
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets);
};

}

#endif