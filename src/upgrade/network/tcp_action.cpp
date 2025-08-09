#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <chrono>

#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/tcp_action.h"
#include "upgrade/network/tcp_socket.h"

namespace quicx {
namespace upgrade {

// TcpAction implementation
bool TcpAction::Init() {
    // Create event driver
    event_driver_ = IEventDriver::Create();
    if (!event_driver_) {
        common::LOG_ERROR("Failed to create event driver");
        return false;
    }
    
    // Initialize event driver
    if (!event_driver_->Init()) {
        common::LOG_ERROR("Failed to initialize event driver");
        return false;
    }
    
    // Create timer
    timer_ = quicx::common::MakeTimer();
    if (!timer_) {
        common::LOG_ERROR("Failed to create timer");
        return false;
    }
    
    // Start event loop thread
    running_ = true;
    event_thread_ = std::thread(&TcpAction::EventLoop, this);
    
    common::LOG_INFO("TCP action initialized with timer support");
    return true;
}

bool TcpAction::AddListener(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) {
    if (!event_driver_) {
        common::LOG_ERROR("TCP action not initialized");
        return false;
    }
    if (port == 0 || port > 65535) {
        common::LOG_ERROR("Invalid listen port: %u", port);
        return false;
    }
    // Create listening socket
    int listen_fd = CreateListenSocket(addr, port);
    if (listen_fd < 0) {
        common::LOG_ERROR("Failed to create listening socket on %s:%d", addr.c_str(), port);
        return false;
    }
    
    // Add listening socket to event driver
    if (!event_driver_->AddFd(listen_fd, EventType::READ)) {
        common::LOG_ERROR("Failed to add listening socket to event driver");
        close(listen_fd);
        return false;
    }
    
    // Store listener
    listeners_[listen_fd] = handler;
    
    common::LOG_INFO("Listener added on %s:%d", addr.c_str(), port);
    return true;
}

void TcpAction::Stop() {
    running_ = false;
    if (event_driver_) {
        event_driver_->Wakeup();
    }
    
    // Close all listening sockets
    for (auto& listener : listeners_) {
        close(listener.first);
    }
    listeners_.clear();
    
    // Close all connections
    connections_.clear();
    
    common::LOG_INFO("TCP action stopped");
}

void TcpAction::Join() {
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    common::LOG_INFO("TCP action joined");
}

int TcpAction::CreateListenSocket(const std::string& addr, uint16_t port) {
    // Create socket
    auto result = common::TcpSocket();
    if (result.errno_ != 0) {
        common::LOG_ERROR("Failed to create socket: %s", strerror(result.errno_));
        return -1;
    }
    uint64_t listen_fd = result.return_value_;

    // Set non-blocking
    auto ret = common::SocketNoblocking(listen_fd);
    if (ret.errno_ != 0) {
        common::LOG_ERROR("Failed to get socket flags: %s", strerror(ret.errno_));
        close(listen_fd);
        return -1;
    }
    
    // Bind socket
    common::Address address(addr, port);
    ret = common::Bind(listen_fd, address);
    if (ret.errno_ != 0) {
        common::LOG_ERROR("Failed to bind socket: %s", strerror(ret.errno_));
        close(listen_fd);
        return -1;
    }
    
    common::LOG_INFO("Listening socket created on %s:%d", addr.c_str(), port);
    return listen_fd;
}

void TcpAction::EventLoop() {
    std::vector<Event> events;
    events.reserve(event_driver_->GetMaxEvents());
    
    common::LOG_INFO("Starting TCP event loop");
    
    while (running_) {
        // Get current time for timer
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        
        // Run timer wheel
        timer_->TimerRun(now);
        
        // Calculate timeout for event driver based on next timer
        int32_t next_timer_ms = timer_->MinTime(now);
        int timeout_ms = 1000; // Default 1 second timeout
        
        if (next_timer_ms >= 0) {
            timeout_ms = static_cast<int>(next_timer_ms);
        }
        
        // Wait for events with timeout
        int nfds = event_driver_->Wait(events, timeout_ms);
        
        if (nfds < 0) {
            // Error occurred
            common::LOG_ERROR("Event driver wait failed");
            break;
        }
        
        if (nfds > 0) {
            // Handle events
            HandleEvents(events);
        }
        
        // Check if we should stop
        if (!running_) {
            break;
        }
    }
    
    common::LOG_INFO("TCP event loop stopped");
}

void TcpAction::HandleEvents(const std::vector<Event>& events) {
    for (const auto& event : events) {
        // Check if this is a listening socket
        auto listener_it = listeners_.find(event.fd);
        if (listener_it != listeners_.end()) {
            // New connection on this listener
            HandleNewConnection(event.fd, listener_it->second);
        } else {
            // Existing connection
            auto it = connections_.find(event.fd);
            if (it != connections_.end()) {
                auto socket = it->second;
                auto handler = socket->GetHandler();
                
                if (handler) {
                    switch (event.type) {
                        case EventType::READ:
                            handler->HandleRead(socket);
                            break;
                        case EventType::WRITE:
                            handler->HandleWrite(socket);
                            break;
                        case EventType::ERROR:
                        case EventType::CLOSE:
                            handler->HandleClose(socket);
                            event_driver_->RemoveFd(event.fd);
                            connections_.erase(it);
                            break;
                    }
                } else {
                    common::LOG_ERROR("No handler found for socket %d", event.fd);
                    event_driver_->RemoveFd(event.fd);
                    connections_.erase(it);
                }
            }
        }
    }
}

void TcpAction::HandleNewConnection(int listen_fd, std::shared_ptr<ISocketHandler> handler) {
    common::Address client_addr;
    auto ret = common::Accept(listen_fd, client_addr);
    if (ret.errno_ != 0) {
        common::LOG_ERROR("Failed to accept connection: %s", strerror(ret.errno_));
        return;
    }
    uint64_t client_fd = ret.return_value_;
    if (client_fd < 0) {
        common::LOG_ERROR("Failed to accept connection: %s", strerror(ret.errno_));
        return;
    }
    
    // Set non-blocking
    auto noblock_ret = common::SocketNoblocking(client_fd);
    if (noblock_ret.errno_ != 0) {
        common::LOG_ERROR("Failed to set socket non-blocking: %s", strerror(noblock_ret.errno_));
        return;
    }
    
    // Create TcpSocket
    auto tcp_socket = std::make_shared<quicx::upgrade::TcpSocket>(client_fd);
    
    // Set handler for the socket
    tcp_socket->SetHandler(handler);
    
    // Add to event driver
    if (!event_driver_->AddFd(client_fd, EventType::READ)) {
        common::LOG_ERROR("Failed to add client socket to event driver");
        close(client_fd);
        return;
    }
    
    // Store connection
    connections_[client_fd] = tcp_socket;
    
    // Notify handler
    handler->HandleConnect(tcp_socket, shared_from_this());
    
    common::LOG_INFO("New connection from %s:%d", client_addr.GetIp().c_str(), client_addr.GetPort());
}

uint64_t TcpAction::AddTimer(std::function<void()> callback, uint32_t timeout_ms) {
    if (!timer_) {
        common::LOG_ERROR("Timer not initialized");
        return 0;
    }
    
    // Create timer task
    quicx::common::TimerTask task(callback);
    
    // Get current time
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    // Add timer to timer wheel
    uint64_t timer_id = timer_->AddTimer(task, timeout_ms, now);
    
    if (timer_id > 0) {
        // Store timer task
        timer_tasks_[timer_id] = task;
        common::LOG_DEBUG("Timer added with ID: %lu, timeout: %u ms", timer_id, timeout_ms);
        // Wake up event loop to recompute timeout based on the new timer
        if (event_driver_) {
            event_driver_->Wakeup();
        }
    } else {
        common::LOG_ERROR("Failed to add timer");
    }
    
    return timer_id;
}

bool TcpAction::RemoveTimer(uint64_t timer_id) {
    if (!timer_) {
        common::LOG_ERROR("Timer not initialized");
        return false;
    }
    
    // Find timer task
    auto it = timer_tasks_.find(timer_id);
    if (it == timer_tasks_.end()) {
        common::LOG_ERROR("Timer not found: %lu", timer_id);
        return false;
    }
    
    // Remove from timer wheel
    bool success = timer_->RmTimer(it->second);
    
    if (success) {
        timer_tasks_.erase(it);
        common::LOG_DEBUG("Timer removed: %lu", timer_id);
    } else {
        common::LOG_ERROR("Failed to remove timer: %lu", timer_id);
    }
    
    return success;
}

} // namespace upgrade
} // namespace quicx 