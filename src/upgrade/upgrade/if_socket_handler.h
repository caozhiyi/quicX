#ifndef UPGRADE_UPGRADE_IF_SOCKET_HANDLER
#define UPGRADE_UPGRADE_IF_SOCKET_HANDLER

#include <unordered_map>
#include "upgrade/upgrade/type.h"
#include "upgrade/network/if_socket.h"
#include "upgrade/include/if_upgrade.h"
#include "common/buffer/if_buffer_read.h"
#include "upgrade/upgrade/if_http_handler.h"

namespace quicx {
namespace upgrade {

/*
 * socket handler interface
 * process read and write socket data
 */
class ISocketHandler {
public:
    ISocketHandler() {}
    virtual ~ISocketHandler() {}

    void RegisterHttpHandler(HttpVersion http_version, std::shared_ptr<IHttpHandler> handler);

    virtual void HandleSocketConnect(uint64_t listen_socket) = 0;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket) = 0;
    virtual void HandleSocketClose(std::shared_ptr<ISocket> socket) = 0;

    virtual void ReadData(std::shared_ptr<ISocket> socket) = 0;
    virtual void WriteData(std::shared_ptr<ISocket> socket) = 0;

protected:
    std::unordered_map<HttpVersion, std::shared_ptr<IHttpHandler>> http_handlers_;
};

}
}


#endif