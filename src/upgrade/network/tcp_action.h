#ifndef UPGRADE_NETWORK_TCP_ACTION_H
#define UPGRADE_NETWORK_TCP_ACTION_H

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/network/if_event_driver.h"
#include "upgrade/network/if_socket_handler.h"

namespace quicx {
namespace upgrade {


// TCP action implementation
class TcpAction : public ITcpAction {
public:
    TcpAction() = default;
    virtual ~TcpAction() = default;

    // Initialize TCP action (call once)
    virtual bool Init() override;
    
    // Add listener with address, port and handler
    virtual bool AddListener(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) override;
    
    // Stop the TCP action
    virtual void Stop() override;
    
    // Wait for TCP action to finish
    virtual void Join() override;

private:
    // Main event loop
    void EventLoop();
    
    // Handle events from event driver
    void HandleEvents(const std::vector<Event>& events);

    // Create listening socket
    int CreateListenSocket(const std::string& addr, uint16_t port);

    // Handle new connection
    void HandleNewConnection(int listen_fd, std::shared_ptr<ISocketHandler> handler);

    std::shared_ptr<IEventDriver> event_driver_;
    std::thread event_thread_;
    std::atomic<bool> running_{false};
    std::unordered_map<int, std::shared_ptr<ISocketHandler>> listeners_;  // fd -> handler
    std::unordered_map<int, std::shared_ptr<ITcpSocket>> connections_;   // fd -> socket
    std::unordered_map<int, std::shared_ptr<ISocketHandler>> connection_handlers_;  // fd -> handler
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_TCP_ACTION_H 