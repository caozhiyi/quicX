#ifndef UPGRADE_HTTP2_HTTP2_PLAIN_HANDLER
#define UPGRADE_HTTP2_HTTP2_PLAIN_HANDLER

#include "upgrade/upgrade/plain_handler.h"

namespace quicx {
namespace upgrade {

class Http2PlainHandler:
    public PlainHandler {
public:
    Http2PlainHandler() {}
    virtual ~Http2PlainHandler() {}

    virtual void HandleSocketData(std::shared_ptr<ISocket> socket) override;
};

}
}


#endif