#ifndef UPGRADE_UPGRADE_IF_SOCKET_HANDLER
#define UPGRADE_UPGRADE_IF_SOCKET_HANDLER

#include <memory>
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

    virtual void HandleConnect(std::shared_ptr<TcpSocket> socket, std::shared_ptr<ITcpAction> action) = 0;
    virtual void HandleRead(std::shared_ptr<TcpSocket> socket) = 0;
    virtual void HandleWrite(std::shared_ptr<TcpSocket> socket) = 0;
    virtual void HandleClose(std::shared_ptr<TcpSocket> socket) = 0;

protected:
    // dispatch socket to http handler(http/1.x or http/2) after data is read
    void DispatchHttpHandler(std::shared_ptr<TcpSocket> socket);
    // build http handler, we can override this function to build different http handler
    // default to build http/1.x and http/2 handler
    virtual void BuildHttpHandler();

protected:
    std::shared_ptr<common::BlockMemoryPool> pool_block_;
    std::unordered_map<uint64_t, std::shared_ptr<TcpSocket>> sockets_;

    std::unique_ptr<IHttpHandler> http_1_handler_;
    std::unique_ptr<IHttpHandler> http_2_handler_;
};

}
}

#endif