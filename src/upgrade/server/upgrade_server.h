#ifndef UPGRADE_SERVER_UPGRADE_SERVER_H
#define UPGRADE_SERVER_UPGRADE_SERVER_H

#include <memory>
#include "common/network/if_event_loop.h"
#include "upgrade/include/if_upgrade.h"

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
    std::vector<uint32_t> listen_fds_;
    // Single TCP action that manages all listeners
    std::shared_ptr<common::IEventLoop> event_loop_;

};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_SERVER_UPGRADE_SERVER_H 