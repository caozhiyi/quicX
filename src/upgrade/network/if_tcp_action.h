#ifndef UPGRADE_NETWORK_IF_TCP_ACTION_H
#define UPGRADE_NETWORK_IF_TCP_ACTION_H

#include <memory>
#include <string>
#include <cstdint>
#include <functional>

namespace quicx {
namespace upgrade {

// Forward declaration
class ITcpSocket;
class ISocketHandler;

// TCP action interface
class ITcpAction {
public:
    virtual ~ITcpAction() = default;

    // Initialize TCP action (call once)
    virtual bool Init() = 0;
    
    // Add listener with address, port and handler
    virtual bool AddListener(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) = 0;
    
    // Stop the TCP action
    virtual void Stop() = 0;
    
    // Wait for TCP action to finish
    virtual void Join() = 0;
    
    // Add timer with callback function and timeout in milliseconds
    virtual uint64_t AddTimer(std::function<void()> callback, uint32_t timeout_ms) = 0;
    
    // Remove timer by ID
    virtual bool RemoveTimer(uint64_t timer_id) = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_IF_TCP_ACTION_H 