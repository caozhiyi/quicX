#ifndef UPGRADE_HTTP2_HTTP2_HANDLER
#define UPGRADE_HTTP2_HTTP2_HANDLER

#include "upgrade/upgrade/if_http_handler.h"

namespace quicx {
namespace upgrade {

class Http2Handler:
    public IHttpHandler {
public:
    Http2Handler() {}
    virtual ~Http2Handler() {}

    virtual void HandleRequest(std::shared_ptr<TcpSocket> socket) override;
};

}
}


#endif