#ifndef UPGRADE_UPGRADE_IF_HTTP_HANDLER
#define UPGRADE_UPGRADE_IF_HTTP_HANDLER

#include "upgrade/network/if_socket.h"
#include "upgrade/include/if_upgrade.h"
#include "common/buffer/if_buffer_read.h"


namespace quicx {
namespace upgrade {

/*
 * http handler interface
 * process http request and response
 */
class IHttpHandler {
public:
    IHttpHandler() {}
    virtual ~IHttpHandler() {}

    virtual void HandleRequest(std::shared_ptr<ISocket> socket) = 0;
};

}
}


#endif