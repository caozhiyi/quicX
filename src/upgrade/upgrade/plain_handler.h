#ifndef UPGRADE_UPGRADE_PLAIN_HANDLER
#define UPGRADE_UPGRADE_PLAIN_HANDLER

#include <functional>
#include <unordered_map>
#include "upgrade/upgrade/type.h"
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class PlainHandler:
    public ISocketHandler {
public:
    PlainHandler() {}
    virtual ~PlainHandler() {}

    virtual void HandleConnect(std::shared_ptr<TcpSocket> socket, std::shared_ptr<ITcpAction> action) override;
    virtual void HandleRead(std::shared_ptr<TcpSocket> socket) override;
    virtual void HandleWrite(std::shared_ptr<TcpSocket> socket) override;
    virtual void HandleClose(std::shared_ptr<TcpSocket> socket) override;
};

}
}

#endif