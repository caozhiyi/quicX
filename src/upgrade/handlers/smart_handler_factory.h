#ifndef UPGRADE_HANDLERS_SMART_HANDLER_FACTORY
#define UPGRADE_HANDLERS_SMART_HANDLER_FACTORY

#include <memory>
#include "upgrade/include/type.h"
#include "upgrade/handlers/if_smart_handler.h"

namespace quicx {
namespace upgrade {

// Factory class for creating smart handlers
class SmartHandlerFactory {
public:
    // Create appropriate smart handler based on settings
    static std::shared_ptr<ISmartHandler> CreateHandler(const UpgradeSettings& settings, std::shared_ptr<ITcpAction> tcp_action);
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_SMART_HANDLER_FACTORY_H 