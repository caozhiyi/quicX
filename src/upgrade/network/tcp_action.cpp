#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common/log/log.h"
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
    
    // Start event loop thread
    running_ = true;
    event_thread_ = std::thread(&TcpAction::EventLoop, this);
    
    common::LOG_INFO("TCP action initialized");
    return true;
}

bool TcpAction::AddListener(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) {
    // Create listening socket
    int listen_fd = CreateListenSocket(addr, port);
    if (listen_fd < 0) {
        common::LOG_ERROR("Failed to create listening socket on %s:%d", addr.c_str(), port);
        return false;
    }
    
    // Add listening socket to event driver
    if (!event_driver_->AddFd(listen_fd, EventType::READ, this)) {
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
    
    // Close all listening sockets
    for (auto& listener : listeners_) {
        close(listener.first);
    }
    listeners_.clear();
    
    // Close all connections
    connections_.clear();
    connection_handlers_.clear();
    
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
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        common::LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        common::LOG_ERROR("Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }
    
    // Set non-blocking
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags < 0) {
        common::LOG_ERROR("Failed to get socket flags: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }
    
    if (fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        common::LOG_ERROR("Failed to set non-blocking: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }
    
    // Bind socket
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = inet_addr(addr.c_str());
    
    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&sock_addr), sizeof(sock_addr)) < 0) {
        common::LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }
    
    // Listen
    if (listen(listen_fd, SOMAXCONN) < 0) {
        common::LOG_ERROR("Failed to listen: %s", strerror(errno));
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
        // Check if this is a listening socket
        auto listener_it = listeners_.find(event.fd);
        if (listener_it != listeners_.end()) {
            // New connection on this listener
            HandleNewConnection(event.fd, listener_it->second);
        } else {
            // Existing connection
            auto it = connections_.find(event.fd);
            auto handler_it = connection_handlers_.find(event.fd);
            if (it != connections_.end() && handler_it != connection_handlers_.end()) {
                auto socket = it->second;
                auto handler = handler_it->second;
                
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
                        connection_handlers_.erase(handler_it);
                        break;
                }
            }
        }
    }
}

void TcpAction::HandleNewConnection(int listen_fd, std::shared_ptr<ISocketHandler> handler) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        common::LOG_ERROR("Failed to accept connection: %s", strerror(errno));
        return;
    }
    
    // Set non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // Create TcpSocket
    auto tcp_socket = std::make_shared<quicx::upgrade::TcpSocket>(client_fd);
    
    // Add to event driver
    if (!event_driver_->AddFd(client_fd, EventType::READ, this)) {
        common::LOG_ERROR("Failed to add client socket to event driver");
        close(client_fd);
        return;
    }
    
    // Store connection with its handler
    connections_[client_fd] = tcp_socket;
    connection_handlers_[client_fd] = handler;
    
    // Notify handler
    handler->HandleConnect(tcp_socket, std::shared_ptr<ITcpAction>(this, [](ITcpAction*){}));
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    common::LOG_INFO("New connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
}

} // namespace upgrade
} // namespace quicx 