#ifndef UPGRADE_HANDLERS_IF_SMART_HANDLER
#define UPGRADE_HANDLERS_IF_SMART_HANDLER

#include <string>

#include "common/network/if_event_loop.h"

namespace quicx {
namespace upgrade {

// Base interface for smart handlers
class ISmartHandler:
    public common::IFdHandler {
public:
    virtual ~ISmartHandler() = default;

    // Get handler type
    virtual std::string GetType() const = 0;

    // Handle connect event
    virtual void OnConnect(uint32_t fd) = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_IF_SMART_HANDLER_H 