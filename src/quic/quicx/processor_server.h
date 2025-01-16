#ifndef QUIC_QUICX_PROCESSOR_SERVER
#define QUIC_QUICX_PROCESSOR_SERVER

#include "quic/quicx/processor_base.h"

namespace quicx {
namespace quic {

/*
 server processor
*/
class ProcessorServer:
    public ProcessorBase {
public:
    ProcessorServer(std::shared_ptr<TLSCtx> ctx,
        connection_state_callback connection_handler);
    virtual ~ProcessorServer();

    virtual void SetServerAlpn(const std::string& alpn);

private:
    bool HandlePacket(std::shared_ptr<INetPacket> packet);
    void SendVersionNegotiatePacket(std::shared_ptr<INetPacket> packet);

private:
    std::string server_alpn_;
};

}
}

#endif