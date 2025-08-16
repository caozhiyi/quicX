#ifndef UPGRADE_HANDLERS_IF_SMART_HANDLER
#define UPGRADE_HANDLERS_IF_SMART_HANDLER

#include <string>
#include "upgrade/network/if_socket_handler.h"

namespace quicx {
namespace upgrade {

// Base interface for smart handlers
class ISmartHandler:
    public ISocketHandler {
public:
    virtual ~ISmartHandler() = default;

    // Get handler type
    virtual std::string GetType() const = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_IF_SMART_HANDLER_H 