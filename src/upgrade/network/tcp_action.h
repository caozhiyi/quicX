#ifndef UPGRADE_NETWORK_TCP_ACTION_H
#define UPGRADE_NETWORK_TCP_ACTION_H

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/network/if_event_driver.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/handlers/if_smart_handler.h"

namespace quicx {
namespace upgrade {

// TCP socket wrapper for backward compatibility
class TcpSocketWrapper : public TcpSocket {
public:
    explicit TcpSocketWrapper(std::shared_ptr<ITcpSocket> socket);
    virtual ~TcpSocketWrapper() = default;

    // Get the underlying TCP socket
    virtual std::shared_ptr<ITcpSocket> GetSocket() const override { return socket_; }

    // Send data
    virtual int Send(const std::string& data) override;

    // Close the socket
    virtual void Close() override;

private:
    std::shared_ptr<ITcpSocket> socket_;
};

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

    std::shared_ptr<ISmartHandler> handler_;
    std::shared_ptr<IEventDriver> event_driver_;
    std::thread event_thread_;
    std::atomic<bool> running_{false};
    std::string listen_addr_;
    uint16_t listen_port_;
    int listen_fd_ = -1;
    std::unordered_map<int, std::shared_ptr<TcpSocketWrapper>> connections_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_TCP_ACTION_H 