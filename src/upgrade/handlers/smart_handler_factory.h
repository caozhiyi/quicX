#ifndef UPGRADE_HANDLERS_SMART_HANDLER_FACTORY
#define UPGRADE_HANDLERS_SMART_HANDLER_FACTORY

#include <memory>
#include <quicx/upgrade/type.h>
#include "upgrade/handlers/if_smart_handler.h"

namespace quicx {
namespace upgrade {

// Factory class for creating smart handlers
class SmartHandlerFactory {
public:
    // Explicit protocol selector for callers that need to bind both an HTTP
    // and an HTTPS listener from the same UpgradeSettings (e.g. running
    // 8080 plaintext + 8443 TLS in the same process so a browser can pick
    // up Alt-Svc on H1/H2 and then jump to H3).
    enum class HandlerKind {
        kAuto,   // legacy: pick based on whether settings carry credentials
        kHttp,   // force plaintext H1/H2 handler
        kHttps,  // force TLS handler (requires credentials in settings)
    };

    // Create appropriate smart handler based on settings
    static std::shared_ptr<ISmartHandler> CreateHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop);

    // Same as above, but the caller picks the protocol explicitly.
    static std::shared_ptr<ISmartHandler> CreateHandler(const UpgradeSettings& settings,
                                                        std::shared_ptr<common::IEventLoop> event_loop,
                                                        HandlerKind kind);
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_SMART_HANDLER_FACTORY_H 