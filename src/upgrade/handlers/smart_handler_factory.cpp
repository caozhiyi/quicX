#include "common/log/log.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "upgrade/handlers/smart_handler_factory.h"

namespace quicx {
namespace upgrade {

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHandler(const UpgradeSettings& settings, std::shared_ptr<ITcpAction> tcp_action) {
    // Treat partial HTTPS settings as HTTP
    const bool has_file_pair = !settings.cert_file.empty() && !settings.key_file.empty();
    const bool has_pem_pair = (settings.cert_pem != nullptr) && (settings.key_pem != nullptr);
    const bool has_https_credentials = has_file_pair || has_pem_pair;

    if (has_https_credentials) {
        common::LOG_INFO("Creating HTTPS smart handler");
        auto https = std::make_shared<HttpsSmartHandler>(settings, tcp_action);
        // If HTTPS handler failed to initialize (e.g., bad certs), fall back to HTTP
        if (!https) {
            common::LOG_WARN("HTTPS handler creation failed, falling back to HTTP");
            return std::make_shared<HttpSmartHandler>(settings, tcp_action);
        }
        return https;
    } else {
        common::LOG_INFO("Creating HTTP smart handler");
        return std::make_shared<HttpSmartHandler>(settings, tcp_action);
    }
}

} // namespace upgrade
} // namespace quicx 