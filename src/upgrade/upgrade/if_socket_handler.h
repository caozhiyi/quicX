#ifndef UPGRADE_UPGRADE_IF_SOCKET_HANDLER
#define UPGRADE_UPGRADE_IF_SOCKET_HANDLER

#include <unordered_map>
#include "upgrade/upgrade/type.h"
#include "common/alloter/pool_block.h"
#include "upgrade/include/if_upgrade.h"
#include "common/buffer/if_buffer_read.h"
#include "upgrade/upgrade/if_http_handler.h"

namespace quicx {
namespace upgrade {

/*
 * socket handler interface
 * process read and write socket data
 */
class TcpSocket;
class ITcpAction;
class ISocketHandler:
    public std::enable_shared_from_this<ISocketHandler> {
public:
    ISocketHandler();
    virtual ~ISocketHandler() {}

    void RegisterHttpHandler(HttpVersion http_version, std::shared_ptr<IHttpHandler> handler);

    virtual void HandleConnect(std::shared_ptr<TcpSocket> socket, ITcpAction* action) = 0;
    virtual void HandleRead(std::shared_ptr<TcpSocket> socket) = 0;
    virtual void HandleWrite(std::shared_ptr<TcpSocket> socket) = 0;
    virtual void HandleClose(std::shared_ptr<TcpSocket> socket) = 0;

protected:
    std::shared_ptr<common::BlockMemoryPool> pool_block_;
    std::unordered_map<uint64_t, std::shared_ptr<TcpSocket>> sockets_;
    std::unordered_map<HttpVersion, std::shared_ptr<IHttpHandler>> http_handlers_;
};

}
}


#endif