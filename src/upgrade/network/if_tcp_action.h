#ifndef UPGRADE_NETWORK_IF_TCP_ACTION_H
#define UPGRADE_NETWORK_IF_TCP_ACTION_H

#include <memory>
#include <string>
#include <cstdint>

namespace quicx {
namespace upgrade {

// Forward declaration
class ITcpSocket;
class ISocketHandler;

// TCP action interface
class ITcpAction {
public:
    virtual ~ITcpAction() = default;

    // Initialize TCP action with address, port and handler
    virtual bool Init(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) = 0;
    
    // Stop the TCP action
    virtual void Stop() = 0;
    
    // Wait for TCP action to finish
    virtual void Join() = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_IF_TCP_ACTION_H 