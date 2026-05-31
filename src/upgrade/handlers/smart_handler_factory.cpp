#include "common/log/log.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "upgrade/handlers/smart_handler_factory.h"

namespace quicx {
namespace upgrade {

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop) {
    // Treat partial HTTPS settings as HTTP
    const bool has_file_pair = !settings.cert_file.empty() && !settings.key_file.empty();
    const bool has_pem_pair = (settings.cert_pem != nullptr) && (settings.key_pem != nullptr);
    const bool has_https_credentials = has_file_pair || has_pem_pair;

    if (has_https_credentials) {
        LOG_INFO("Creating HTTPS smart handler");
        auto https = std::make_shared<HttpsSmartHandler>(settings, event_loop);
        // If HTTPS handler failed to initialize (e.g., bad certs), fall back to HTTP
        if (!https) {
            LOG_WARN("HTTPS handler creation failed, falling back to HTTP");
            return std::make_shared<HttpSmartHandler>(settings, event_loop);
        }
        return https;
    } else {
        LOG_INFO("Creating HTTP smart handler");
        return std::make_shared<HttpSmartHandler>(settings, event_loop);
    }
}

std::shared_ptr<ISmartHandler> SmartHandlerFactory::CreateHandler(const UpgradeSettings& settings,
                                                                  std::shared_ptr<common::IEventLoop> event_loop,
                                                                  HandlerKind kind) {
    switch (kind) {
        case HandlerKind::kHttp:
            LOG_INFO("Creating HTTP smart handler (forced)");
            return std::make_shared<HttpSmartHandler>(settings, event_loop);

        case HandlerKind::kHttps: {
            const bool has_file_pair = !settings.cert_file.empty() && !settings.key_file.empty();
            const bool has_pem_pair = (settings.cert_pem != nullptr) && (settings.key_pem != nullptr);
            if (!has_file_pair && !has_pem_pair) {
                LOG_ERROR("HTTPS handler requested but no certificate/key configured");
                return nullptr;
            }
            LOG_INFO("Creating HTTPS smart handler (forced)");
            return std::make_shared<HttpsSmartHandler>(settings, event_loop);
        }

        case HandlerKind::kAuto:
        default:
            return CreateHandler(settings, event_loop);
    }
}

} // namespace upgrade
} // namespace quicx