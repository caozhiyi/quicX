#ifndef QUIC_QUICX_IF_PROCESSOR
#define QUIC_QUICX_IF_PROCESSOR

#include <memory>
#include <functional>
#include "common/thread/thread.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "common/timer/timer_interface.h"
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

class IProcessor:
    public common::Thread {
public:
    IProcessor() {}
    virtual ~IProcessor() {}

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet) = 0;
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets) = 0;

    virtual void ActiveSendConnection(std::shared_ptr<IConnection> conn) = 0;

    virtual void WeakUp() = 0;

    virtual std::shared_ptr<IConnection> MakeClientConnection() = 0;

    typedef std::function<std::shared_ptr<UdpPacketIn>/*recv packet*/()> RecvFunction;
    void SetRecvFunction(RecvFunction rf) { _recv_function = rf; }

    std::shared_ptr<TLSCtx> GetCtx() { return _ctx; }

    void SetAddConnectionIDCB(std::function<void(uint64_t/*cid hash*/)> cb) { _add_connection_id_cb = cb; }
    void SetRetireConnectionIDCB(std::function<void(uint64_t/*cid hash*/)> cb)  { _retire_connection_id_cb = cb; }

protected:
    std::shared_ptr<TLSCtx> _ctx;
    RecvFunction _recv_function;
    std::function<void(uint64_t/*cid hash*/)> _add_connection_id_cb;
    std::function<void(uint64_t/*cid hash*/)> _retire_connection_id_cb;
};

}
}

#endif