#ifndef UPGRADE_HTTP2_HTTP2_TLS_HANDLER
#define UPGRADE_HTTP2_HTTP2_TLS_HANDLER

#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class Http2TlsHandler:
    public IfSocketHandler {
public:
    Http2TlsHandler() {}
    virtual ~Http2TlsHandler() {}

    virtual void HandleSocketConnect(std::shared_ptr<ISocket> socket) override;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket, std::shared_ptr<common::IBufferRead> buffer) override;
};

}
}


#endif