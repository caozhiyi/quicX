#ifndef QUIC_PROCESS_SERVER_PROCESSOR
#define QUIC_PROCESS_SERVER_PROCESSOR

#include "quic/process/processor_interface.h"

namespace quicx {

class ServerProcessor:
    public IProcessor {
public:
    ServerProcessor();
    virtual ~ServerProcessor();

    virtual bool HandlePacket(std::shared_ptr<IUdpPacket> udp_packet);
    virtual bool HandlePackets(const std::vector<std::shared_ptr<IUdpPacket>>& udp_packets);
};

}

#endif