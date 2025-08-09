#include "upgrade/handlers/smart_handler_factory.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "common/log/log.h"

namespace quicx {
namespace upgrade {

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHandler(const UpgradeSettings& settings) {
    // Treat partial HTTPS settings as HTTP
    const bool has_file_pair = !settings.cert_file.empty() && !settings.key_file.empty();
    const bool has_pem_pair = (settings.cert_pem != nullptr) && (settings.key_pem != nullptr);
    const bool has_https_credentials = has_file_pair || has_pem_pair;

    if (has_https_credentials) {
        common::LOG_INFO("Creating HTTPS smart handler");
        auto https = CreateHttpsHandler(settings);
        // If HTTPS handler failed to initialize (e.g., bad certs), fall back to HTTP
        if (!https) {
            common::LOG_WARN("HTTPS handler creation failed, falling back to HTTP");
            return CreateHttpHandler(settings);
        }
        return https;
    } else {
        common::LOG_INFO("Creating HTTP smart handler");
        return CreateHttpHandler(settings);
    }
}

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHttpHandler(const UpgradeSettings& settings) {
    return std::make_shared<HttpSmartHandler>(settings);
}

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHttpsHandler(const UpgradeSettings& settings) {
    auto handler = std::make_shared<HttpsSmartHandler>(settings);
    // If SSL could not be initialized (ssl_ready_ == false), return nullptr to allow fallback
    // We can't access ssl_ready_ directly; rely on behavior: if HTTPS cannot init, InitializeConnection will fail.
    // Here we conservatively return the handler; factory CreateHandler already falls back only if nullptr.
    return handler;
}

} // namespace upgrade
} // namespace quicx 