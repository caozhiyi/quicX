#ifndef QUIC_QUICX_PROCESSOR_INTERFACE
#define QUIC_QUICX_PROCESSOR_INTERFACE

#include <memory>
#include "common/thread/thread.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "common/timer/timer_interface.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class IProcessor:
    public Thread {
public:
    IProcessor() {}
    virtual ~IProcessor() {}

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet) = 0;
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets) = 0;

    virtual void ActiveSendConnection(IConnection* conn) = 0;

    virtual void WeakUp() = 0;

    virtual std::shared_ptr<IConnection> MakeClientConnection() = 0;

    typedef std::function<std::shared_ptr<UdpPacketIn>/*recv packet*/()> RecvFunction;
    void SetRecvFunction(RecvFunction rf) { _recv_function = rf; }

    std::shared_ptr<TLSCtx> GetCtx() { return _ctx; }

    void SetAddConnectionIDCB(ConnectionIDCB cb) { _add_connection_id_cb = cb; }
    void SetRetireConnectionIDCB(ConnectionIDCB cb)  { _retire_connection_id_cb = cb; }

protected:
    std::shared_ptr<TLSCtx> _ctx;
    ConnectionIDCB _add_connection_id_cb;
    ConnectionIDCB _retire_connection_id_cb;
    RecvFunction _recv_function;
};

}

#endif