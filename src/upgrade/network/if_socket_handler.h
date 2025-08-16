#ifndef UPGRADE_NETWORK_IF_SOCKET_HANDLER
#define UPGRADE_NETWORK_IF_SOCKET_HANDLER

#include <memory>
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/network/if_tcp_action.h"

namespace quicx {
namespace upgrade {

// Socket handler interface
class ISocketHandler {
public:
    virtual ~ISocketHandler() = default;

    // Handle new connection
    virtual void HandleConnect(std::shared_ptr<ITcpSocket> socket) = 0;
    
    // Handle data read from socket
    virtual void HandleRead(std::shared_ptr<ITcpSocket> socket) = 0;
    
    // Handle data write to socket
    virtual void HandleWrite(std::shared_ptr<ITcpSocket> socket) = 0;
    
    // Handle socket close
    virtual void HandleClose(std::shared_ptr<ITcpSocket> socket) = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_UPGRADE_IF_SOCKET_HANDLER_H 