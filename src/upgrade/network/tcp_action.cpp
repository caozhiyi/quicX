#include "upgrade/network/tcp_action.h"
#include "common/log/log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace quicx {
namespace upgrade {

// TcpSocketWrapper implementation
TcpSocketWrapper::TcpSocketWrapper(std::shared_ptr<ITcpSocket> socket) 
    : socket_(socket) {
}

int TcpSocketWrapper::Send(const std::string& data) {
    if (!socket_) {
        return -1;
    }
    return socket_->Send(data);
}

void TcpSocketWrapper::Close() {
    if (socket_) {
        socket_->Close();
    }
}

// TcpAction implementation
bool TcpAction::Init(const std::string& addr, uint16_t port, std::shared_ptr<ISmartHandler> handler) {
    handler_ = handler;
    listen_addr_ = addr;
    listen_port_ = port;
    
    // Create listening socket
    if (!CreateListenSocket()) {
        common::LOG_ERROR("Failed to create listening socket");
        return false;
    }
    
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
    
    // Add listening socket to event driver
    if (!event_driver_->AddFd(listen_fd_, EventType::READ, this)) {
        common::LOG_ERROR("Failed to add listening socket to event driver");
        return false;
    }
    
    // Start event loop thread
    running_ = true;
    event_thread_ = std::thread(&TcpAction::EventLoop, this);
    
    common::LOG_INFO("TCP action initialized on %s:%d", addr.c_str(), port);
    return true;
}

void TcpAction::Stop() {
    running_ = false;
    
    // Close listening socket
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    
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

bool TcpAction::CreateListenSocket() {
    // Create socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        common::LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        common::LOG_ERROR("Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    if (flags < 0) {
        common::LOG_ERROR("Failed to get socket flags: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    
    if (fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        common::LOG_ERROR("Failed to set non-blocking: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    
    // Bind socket
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port_);
    addr.sin_addr.s_addr = inet_addr(listen_addr_.c_str());
    
    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        common::LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    
    // Listen
    if (listen(listen_fd_, SOMAXCONN) < 0) {
        common::LOG_ERROR("Failed to listen: %s", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    
    common::LOG_INFO("Listening socket created on %s:%d", listen_addr_.c_str(), listen_port_);
    return true;
}

void TcpAction::EventLoop() {
    std::vector<Event> events;
    events.reserve(event_driver_->GetMaxEvents());
    
    common::LOG_INFO("Starting TCP event loop");
    
    while (running_) {
        // Wait for events with timeout
        int nfds = event_driver_->Wait(events, 1000); // 1 second timeout
        
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
        if (event.fd == listen_fd_) {
            // New connection
            HandleNewConnection();
        } else {
            // Existing connection
            auto it = connections_.find(event.fd);
            if (it != connections_.end()) {
                auto socket_wrapper = it->second;
                
                switch (event.type) {
                    case EventType::READ:
                        handler_->HandleRead(socket_wrapper->GetSocket());
                        break;
                    case EventType::WRITE:
                        handler_->HandleWrite(socket_wrapper->GetSocket());
                        break;
                    case EventType::ERROR:
                    case EventType::CLOSE:
                        handler_->HandleClose(socket_wrapper->GetSocket());
                        event_driver_->RemoveFd(event.fd);
                        connections_.erase(it);
                        break;
                }
            }
        }
    }
}

void TcpAction::HandleNewConnection() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        common::LOG_ERROR("Failed to accept connection: %s", strerror(errno));
        return;
    }
    
    // Set non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // Create TcpSocket wrapper
    auto tcp_socket = std::make_shared<quicx::upgrade::TcpSocket>(client_fd);
    auto socket_wrapper = std::make_shared<TcpSocketWrapper>(tcp_socket);
    
    // Add to event driver
    if (!event_driver_->AddFd(client_fd, EventType::READ, this)) {
        common::LOG_ERROR("Failed to add client socket to event driver");
        close(client_fd);
        return;
    }
    
    // Store connection
    connections_[client_fd] = socket_wrapper;
    
    // Notify handler
    handler_->HandleConnect(socket_wrapper->GetSocket(), std::shared_ptr<ITcpAction>(this, [](ITcpAction*){}));
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    common::LOG_INFO("New connection from {}:{}", client_ip, ntohs(client_addr.sin_port));
}

} // namespace upgrade
} // namespace quicx 