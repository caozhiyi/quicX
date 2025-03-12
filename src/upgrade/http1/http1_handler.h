#ifndef UPGRADE_HTTP1_HTTP1_HANDLER
#define UPGRADE_HTTP1_HTTP1_HANDLER

#include "upgrade/upgrade/if_http_handler.h"

namespace quicx {
namespace upgrade {

class Http1Handler:
    public IHttpHandler {
public:
    Http1Handler() {}
    virtual ~Http1Handler() {}

    virtual void HandleRequest(std::shared_ptr<TcpSocket> socket) override;
};

}
}


#endif