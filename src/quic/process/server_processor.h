#ifndef QUIC_PROCESS_SERVER_PROCESSOR
#define QUIC_PROCESS_SERVER_PROCESSOR

#include "quic/udp/udp_packet_in.h"
#include "common/timer/timer_interface.h"
#include "quic/process/processor_interface.h"

namespace quicx {

class ServerProcessor:
    public IProcessor {
public:
    ServerProcessor();
    virtual ~ServerProcessor();

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet, std::shared_ptr<ITimer> timer);
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets, std::shared_ptr<ITimer> timer);

    bool InitPacketCheck(std::shared_ptr<IPacket> packet);
};

}

#endif