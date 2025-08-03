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
#include "common/timer/timer.h"

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
    
    // Add timer with callback function and timeout in milliseconds
    virtual uint64_t AddTimer(std::function<void()> callback, uint32_t timeout_ms) override;
    
    // Remove timer by ID
    virtual bool RemoveTimer(uint64_t timer_id) override;

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
    
    // Timer support
    std::shared_ptr<quicx::common::ITimer> timer_;
    std::unordered_map<uint64_t, quicx::common::TimerTask> timer_tasks_;  // timer_id -> task
    uint64_t next_timer_id_ = 1;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_TCP_ACTION_H 