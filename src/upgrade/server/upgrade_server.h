#ifndef UPGRADE_SERVER_UPGRADE_SERVER_H
#define UPGRADE_SERVER_UPGRADE_SERVER_H

#include <memory>
#include <utility>
#include <vector>
#include <quicx/common/if_event_loop.h>
#include <quicx/upgrade/if_upgrade.h>

#include "upgrade/server/connection_handler.h"

namespace quicx {
namespace upgrade {

// Main upgrade server implementation
class UpgradeServer:
    public IUpgrade {
public:
    UpgradeServer(std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~UpgradeServer();
    
    // Add listener with specified settings
    virtual bool AddListener(UpgradeSettings& settings) override;
private:
    // Create listening socket
    int CreateListenSocket(const std::string& addr, uint16_t port);

private:
    // EventLoop::fd_to_handler_ stores handlers as std::weak_ptr, so the
    // upgrade server itself MUST own a strong reference to every
    // ConnectionHandler we register, otherwise the shared_ptr created
    // inside AddListener() dies as soon as the local variable goes out of
    // scope and the next epoll/kqueue wakeup logs
    //   "No handler found for fd N"   (or "Handler expired for fd N")
    // and silently drops the accept(). Each entry pairs the listening fd
    // with the ConnectionHandler that should service it; both are released
    // together in ~UpgradeServer().
    struct ListenEntry {
        uint32_t                            fd = 0;
        std::shared_ptr<ConnectionHandler>  handler;
    };
    std::vector<ListenEntry> listeners_;

    // Single TCP action that manages all listeners
    std::weak_ptr<common::IEventLoop> event_loop_;

};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_SERVER_UPGRADE_SERVER_H 