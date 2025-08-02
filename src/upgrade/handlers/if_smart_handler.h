#ifndef UPGRADE_HANDLERS_IF_SMART_HANDLER_H
#define UPGRADE_HANDLERS_IF_SMART_HANDLER_H

#include <memory>
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/include/type.h"

namespace quicx {
namespace upgrade {

// Base interface for smart handlers
class ISmartHandler {
public:
    virtual ~ISmartHandler() = default;

    // Handle new connection
    virtual void HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) = 0;
    
    // Handle data read from socket
    virtual void HandleRead(std::shared_ptr<ITcpSocket> socket) = 0;
    
    // Handle data write to socket
    virtual void HandleWrite(std::shared_ptr<ITcpSocket> socket) = 0;
    
    // Handle socket close
    virtual void HandleClose(std::shared_ptr<ITcpSocket> socket) = 0;
    
    // Get handler type
    virtual std::string GetType() const = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_IF_SMART_HANDLER_H 