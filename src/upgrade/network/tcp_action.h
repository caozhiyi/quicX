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
#include "upgrade/handlers/if_smart_handler.h"

namespace quicx {
namespace upgrade {


// TCP action implementation
class TcpAction : public ITcpAction {
public:
    TcpAction() = default;
    virtual ~TcpAction() = default;

    // Initialize TCP action with address, port and handler
    virtual bool Init(const std::string& addr, uint16_t port, std::shared_ptr<ISmartHandler> handler) override;
    
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
    bool CreateListenSocket();

    // Handle new connection
    void HandleNewConnection();

    std::shared_ptr<ISmartHandler> handler_;
    std::shared_ptr<IEventDriver> event_driver_;
    std::thread event_thread_;
    std::atomic<bool> running_{false};
    std::string listen_addr_;
    uint16_t listen_port_;
    int listen_fd_ = -1;
    std::unordered_map<int, std::shared_ptr<ITcpSocket>> connections_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_TCP_ACTION_H 