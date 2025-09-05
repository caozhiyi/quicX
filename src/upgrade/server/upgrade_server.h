#ifndef UPGRADE_SERVER_UPGRADE_SERVER_H
#define UPGRADE_SERVER_UPGRADE_SERVER_H

#include <memory>
#include <vector>
#include <atomic>
#include "upgrade/include/if_upgrade.h"
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/handlers/if_smart_handler.h"

namespace quicx {
namespace upgrade {

// Main upgrade server implementation
class UpgradeServer:
    public IUpgrade {
public:
    UpgradeServer();
    virtual ~UpgradeServer() = default;

    // Initialize the upgrade server
    virtual bool Init(LogLevel level = LogLevel::kNull) override;
    
    // Add listener with specified settings
    virtual bool AddListener(UpgradeSettings& settings) override;
    
    // Stop the upgrade server
    virtual void Stop() override;
    
    // Wait for the server to finish
    virtual void Join() override;

private:
    // Single TCP action that manages all listeners
    std::shared_ptr<ITcpAction> tcp_action_;
    std::vector<std::shared_ptr<ISmartHandler>> handlers_;
    std::atomic<bool> running_{false};
    LogLevel log_level_ = LogLevel::kNull;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_SERVER_UPGRADE_SERVER_H 