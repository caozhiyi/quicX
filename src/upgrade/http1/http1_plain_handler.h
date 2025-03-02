#ifndef UPGRADE_HTTP1_HTTP1_PLAIN_HANDLER
#define UPGRADE_HTTP1_HTTP1_PLAIN_HANDLER

#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class Http1PlainHandler:
    public IfSocketHandler {
public:
    Http1PlainHandler() {}
    virtual ~Http1PlainHandler() {}

    virtual void HandleSocketConnect(std::shared_ptr<ISocket> socket) override;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket, std::shared_ptr<common::IBufferRead> buffer) override;
};

}
}


#endif