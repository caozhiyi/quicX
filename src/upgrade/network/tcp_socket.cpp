#include <cstring>

#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/tcp_socket.h"

namespace quicx {
namespace upgrade {

TcpSocket::TcpSocket():
    fd_(-1) {

    auto result = common::TcpSocket();

    if (result.errno_ != 0) {
        common::LOG_ERROR("Failed to create socket: %d", result.errno_);
        fd_ = -1;  // Ensure fd_ is -1 on failure
    } else {
        fd_ = result.return_value_;
    }
}

TcpSocket::TcpSocket(int fd):
    fd_(fd) {
    common::ParseRemoteAddress(fd, remote_address_);
}

TcpSocket::TcpSocket(int fd, const common::Address& remote_address) :
    fd_(fd),
    remote_address_(remote_address) {

}

TcpSocket::~TcpSocket() {
    Close();
}

int TcpSocket::Send(const std::vector<uint8_t>& data) {
    if (!IsValid() || data.empty()) {
        return -1;
    }

    auto result = common::Write(fd_, reinterpret_cast<const char*>(data.data()), data.size());
    if (result.errno_ != 0) {
        common::LOG_ERROR("Send failed: %d", result.errno_);
    }
    return result.return_value_;
}

int TcpSocket::Send(const std::string& data) {
    if (!IsValid() || data.empty()) {
        return -1;
    }

    auto result = common::Write(fd_, data.c_str(), data.size());

    if (result.errno_ != 0) {
        common::LOG_ERROR("Send failed: %d", result.errno_);
    }
    return result.return_value_;
}

int TcpSocket::Recv(std::vector<uint8_t>& data, size_t max_size) {
    if (!IsValid()) {
        return -1;
    }

    data.resize(max_size);
    
    auto result = common::Recv(fd_, reinterpret_cast<char*>(data.data()), max_size, 0);

    if (result.errno_ != 0) {
        common::LOG_ERROR("Recv failed: %d", result.errno_);
        data.clear();
        return -1;
    }

    data.resize(result.return_value_);
    return result.return_value_;
}

int TcpSocket::Recv(std::string& data, size_t max_size) {
    if (!IsValid()) {
        return -1;
    }

    std::vector<char> buffer(max_size);
    
    auto result = common::Recv(fd_, buffer.data(), max_size, 0);

    if (result.errno_ != 0) {
        common::LOG_ERROR("Recv failed: %d", result.errno_);
        data.clear();
        return -1;
    }

    data.assign(buffer.data(), result.return_value_);
    return result.return_value_;
}

void TcpSocket::Close() {
    if (IsValid()) {
        auto result = common::Close(fd_);
        if (result.errno_ != 0) {
            common::LOG_ERROR("Close failed: %d", result.errno_);
        }
        fd_ = -1;
    }
}

std::string TcpSocket::GetRemoteAddress() const {
    return remote_address_.GetIp();
}

uint16_t TcpSocket::GetRemotePort() const {
    return remote_address_.GetPort();
}

} // namespace upgrade
} // namespace quicx 