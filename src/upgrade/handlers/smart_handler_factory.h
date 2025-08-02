#ifndef UPGRADE_HANDLERS_SMART_HANDLER_FACTORY_H
#define UPGRADE_HANDLERS_SMART_HANDLER_FACTORY_H

#include <memory>
#include "upgrade/handlers/if_smart_handler.h"
#include "upgrade/include/type.h"

namespace quicx {
namespace upgrade {

// Factory class for creating smart handlers
class SmartHandlerFactory {
public:
    // Create appropriate smart handler based on settings
    static std::shared_ptr<ISmartHandler> CreateHandler(const UpgradeSettings& settings);
private:
    // Create HTTP handler for plain text connections
    static std::shared_ptr<ISmartHandler> CreateHttpHandler(const UpgradeSettings& settings);
    
    // Create HTTPS handler for SSL/TLS connections
    static std::shared_ptr<ISmartHandler> CreateHttpsHandler(const UpgradeSettings& settings);
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_SMART_HANDLER_FACTORY_H 