#include <cstring>
#include <errno.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "common/log/log.h"
#include "upgrade/network/tcp_socket.h"

namespace quicx {
namespace upgrade {

TcpSocket::TcpSocket() : fd_(-1) {
    // Create a new socket
#ifdef _WIN32
    fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
#endif

    if (fd_ < 0) {
        common::LOG_ERROR("Failed to create socket");
    }
}

TcpSocket::TcpSocket(int fd) : fd_(fd) {
    // Constructor with existing file descriptor
}

TcpSocket::~TcpSocket() {
    Close();
}

int TcpSocket::Send(const std::vector<uint8_t>& data) {
    if (!IsValid() || data.empty()) {
        return -1;
    }

#ifdef _WIN32
    int result = send(fd_, reinterpret_cast<const char*>(data.data()), 
                     static_cast<int>(data.size()), 0);
#else
    int result = send(fd_, data.data(), data.size(), 0);
#endif

    if (result < 0) {
        common::LOG_ERROR("Send failed: %s", strerror(errno));
    }

    return result;
}

int TcpSocket::Send(const std::string& data) {
    if (!IsValid() || data.empty()) {
        return -1;
    }

#ifdef _WIN32
    int result = send(fd_, data.c_str(), static_cast<int>(data.size()), 0);
#else
    int result = send(fd_, data.c_str(), data.size(), 0);
#endif

    if (result < 0) {
        common::LOG_ERROR("Send failed: %s", strerror(errno));
    }

    return result;
}

int TcpSocket::Recv(std::vector<uint8_t>& data, size_t max_size) {
    if (!IsValid()) {
        return -1;
    }

    data.resize(max_size);
    
#ifdef _WIN32
    int result = recv(fd_, reinterpret_cast<char*>(data.data()), 
                     static_cast<int>(max_size), 0);
#else
    int result = recv(fd_, data.data(), max_size, 0);
#endif

    if (result < 0) {
        common::LOG_ERROR("Recv failed: %s", strerror(errno));
        data.clear();
        return -1;
    }

    data.resize(result);
    return result;
}

void TcpSocket::SetHandler(std::shared_ptr<ISocketHandler> handler) {
    handler_ = handler;
}

std::shared_ptr<ISocketHandler> TcpSocket::GetHandler() const {
    return handler_.lock();
}

int TcpSocket::Recv(std::string& data, size_t max_size) {
    if (!IsValid()) {
        return -1;
    }

    std::vector<char> buffer(max_size);
    
#ifdef _WIN32
    int result = recv(fd_, buffer.data(), static_cast<int>(max_size), 0);
#else
    int result = recv(fd_, buffer.data(), max_size, 0);
#endif

    if (result < 0) {
        common::LOG_ERROR("Recv failed: %s", strerror(errno));
        data.clear();
        return -1;
    }

    data.assign(buffer.data(), result);
    return result;
}

void TcpSocket::Close() {
    if (IsValid()) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
        fd_ = -1;
        address_cached_ = false;
    }
}

std::string TcpSocket::GetRemoteAddress() const {
    if (!address_cached_) {
        CacheAddressInfo();
    }
    return remote_address_;
}

uint16_t TcpSocket::GetRemotePort() const {
    if (!address_cached_) {
        CacheAddressInfo();
    }
    return remote_port_;
}

std::string TcpSocket::GetLocalAddress() const {
    if (!address_cached_) {
        CacheAddressInfo();
    }
    return local_address_;
}

uint16_t TcpSocket::GetLocalPort() const {
    if (!address_cached_) {
        CacheAddressInfo();
    }
    return local_port_;
}

bool TcpSocket::SetNonBlocking(bool non_blocking) {
    if (!IsValid()) {
        return false;
    }

#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    if (ioctlsocket(fd_, FIONBIO, &mode) != 0) {
        common::LOG_ERROR("Failed to set non-blocking mode");
        return false;
    }
#else
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        common::LOG_ERROR("Failed to get socket flags");
        return false;
    }

    if (non_blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd_, F_SETFL, flags) < 0) {
        common::LOG_ERROR("Failed to set non-blocking mode");
        return false;
    }
#endif

    return true;
}

bool TcpSocket::SetReuseAddr(bool reuse) {
    if (!IsValid()) {
        return false;
    }

    int optval = reuse ? 1 : 0;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&optval), sizeof(optval)) < 0) {
        common::LOG_ERROR("Failed to set SO_REUSEADDR");
        return false;
    }

    return true;
}

bool TcpSocket::SetKeepAlive(bool keep_alive) {
    if (!IsValid()) {
        return false;
    }

    int optval = keep_alive ? 1 : 0;
    if (setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, 
                   reinterpret_cast<const char*>(&optval), sizeof(optval)) < 0) {
        common::LOG_ERROR("Failed to set SO_KEEPALIVE");
        return false;
    }

    return true;
}

void TcpSocket::CacheAddressInfo() const {
    if (!IsValid()) {
        return;
    }

    struct sockaddr_in remote_addr, local_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    // Get remote address
    if (getpeername(fd_, reinterpret_cast<struct sockaddr*>(&remote_addr), &addr_len) == 0) {
        char addr_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &remote_addr.sin_addr, addr_str, INET_ADDRSTRLEN)) {
            remote_address_ = addr_str;
            remote_port_ = ntohs(remote_addr.sin_port);
        }
    }

    // Get local address
    if (getsockname(fd_, reinterpret_cast<struct sockaddr*>(&local_addr), &addr_len) == 0) {
        char addr_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &local_addr.sin_addr, addr_str, INET_ADDRSTRLEN)) {
            local_address_ = addr_str;
            local_port_ = ntohs(local_addr.sin_port);
        }
    }

    address_cached_ = true;
}

} // namespace upgrade
} // namespace quicx 