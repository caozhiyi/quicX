#ifndef UPGRADE_HTTP2_HTTP2_PLAIN_HANDLER
#define UPGRADE_HTTP2_HTTP2_PLAIN_HANDLER

#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class Http2PlainHandler:
    public IfSocketHandler {
public:
    Http2PlainHandler() {}
    virtual ~Http2PlainHandler() {}

    virtual void HandleSocketConnect(std::shared_ptr<ISocket> socket) override;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket, std::shared_ptr<common::IBufferRead> buffer) override;
};

}
}


#endif