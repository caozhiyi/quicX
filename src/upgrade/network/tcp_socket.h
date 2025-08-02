#ifndef UPGRADE_NETWORK_TCP_SOCKET_H
#define UPGRADE_NETWORK_TCP_SOCKET_H

#include <memory>
#include "upgrade/network/if_tcp_socket.h"

namespace quicx {
namespace upgrade {

// TCP socket implementation
class TcpSocket:
    public ITcpSocket {
public:
    TcpSocket();
    explicit TcpSocket(int fd);
    virtual ~TcpSocket();

    // Get the socket file descriptor
    virtual int GetFd() const override { return fd_; }

    // Send data
    virtual int Send(const std::vector<uint8_t>& data) override;
    virtual int Send(const std::string& data) override;

    // Receive data
    virtual int Recv(std::vector<uint8_t>& data, size_t max_size = 4096) override;
    virtual int Recv(std::string& data, size_t max_size = 4096) override;

    // Close the socket
    virtual void Close() override;

    // Check if socket is valid
    virtual bool IsValid() const override { return fd_ >= 0; }

    // Get remote address information
    virtual std::string GetRemoteAddress() const override;
    virtual uint16_t GetRemotePort() const override;

    // Get local address information
    virtual std::string GetLocalAddress() const override;
    virtual uint16_t GetLocalPort() const override;

    // Set socket to non-blocking mode
    bool SetNonBlocking(bool non_blocking = true);

    // Set socket options
    bool SetReuseAddr(bool reuse = true);
    bool SetKeepAlive(bool keep_alive = true);

private:
    int fd_ = -1;
    mutable std::string remote_address_;
    mutable uint16_t remote_port_ = 0;
    mutable std::string local_address_;
    mutable uint16_t local_port_ = 0;
    mutable bool address_cached_ = false;

    // Cache address information
    void CacheAddressInfo() const;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_TCP_SOCKET_H 