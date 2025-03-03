#ifndef UPGRADE_HTTP1_HTTP1_PLAIN_HANDLER
#define UPGRADE_HTTP1_HTTP1_PLAIN_HANDLER

#include "upgrade/upgrade/plain_handler.h"

namespace quicx {
namespace upgrade {

class Http1PlainHandler:
    public PlainHandler {
public:
    Http1PlainHandler() {}
    virtual ~Http1PlainHandler() {}

    virtual void HandleSocketData(std::shared_ptr<ISocket> socket) override;
};

}
}


#endif