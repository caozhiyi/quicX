#include "upgrade/handlers/smart_handler_factory.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "common/log/log.h"

namespace quicx {
namespace upgrade {

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHandler(const UpgradeSettings& settings) {
    if (settings.IsHTTPSEnabled()) {
        common::LOG_INFO("Creating HTTPS smart handler");
        return CreateHttpsHandler(settings);
    } else {
        common::LOG_INFO("Creating HTTP smart handler");
        return CreateHttpHandler(settings);
    }
}

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHttpHandler(const UpgradeSettings& settings) {
    return std::make_shared<HttpSmartHandler>(settings);
}

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHttpsHandler(const UpgradeSettings& settings) {
    return std::make_shared<HttpsSmartHandler>(settings);
}

} // namespace upgrade
} // namespace quicx 