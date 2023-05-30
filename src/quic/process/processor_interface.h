#ifndef QUIC_PROCESS_PROCESSOR_INTERFACE
#define QUIC_PROCESS_PROCESSOR_INTERFACE

#include <memory>
#include <unordered_map>
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/udp/udp_packet_interface.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class IProcessor {
public:
    IProcessor();
    virtual ~IProcessor() {}

    virtual bool HandlePacket(std::shared_ptr<IUdpPacket> udp_packet) = 0;
    virtual bool HandlePackets(const std::vector<std::shared_ptr<IUdpPacket>>& udp_packets) = 0;

    std::shared_ptr<TLSCtx> GetCtx() { return _ctx; }

protected:
    bool GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);

protected:
    std::shared_ptr<TLSCtx> _ctx;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;
};

}

#endif