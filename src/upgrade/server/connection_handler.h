#ifndef UPGRADE_SERVER_CONNECTION_HANDLER
#define UPGRADE_SERVER_CONNECTION_HANDLER

#include <memory>

#include "common/network/if_event_loop.h"

#include "upgrade/handlers/if_smart_handler.h"

namespace quicx {
namespace upgrade {

// TCP action implementation
class ConnectionHandler: public common::IFdHandler {
public:
    ConnectionHandler(std::shared_ptr<common::IEventLoop> event_loop, std::shared_ptr<ISmartHandler> handler):
        handler_(handler),
        event_loop_(event_loop) {}
    virtual ~ConnectionHandler() = default;

    virtual void OnRead(uint32_t fd) override;
    virtual void OnWrite(uint32_t fd) override;
    virtual void OnError(uint32_t fd) override;
    virtual void OnClose(uint32_t fd) override;

    // Tear down every client connection this listener has accepted. Used by
    // the server's shutdown path so no client fd survives its event loop.
    void CloseAllConnections();

private:
    std::shared_ptr<ISmartHandler> handler_;
    std::weak_ptr<common::IEventLoop> event_loop_;
};

}  // namespace upgrade
}  // namespace quicx

#endif  // UPGRADE_SERVER_CONNECTION_HANDLER