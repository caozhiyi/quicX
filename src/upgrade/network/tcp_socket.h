#ifndef UPGRADE_NETWORK_TCP_SOCKET
#define UPGRADE_NETWORK_TCP_SOCKET

#include <memory>

#include "common/network/address.h"
#include "upgrade/network/if_tcp_socket.h"

namespace quicx {
namespace upgrade {

// TCP socket implementation
class TcpSocket:
    public ITcpSocket {
public:
    TcpSocket();
    explicit TcpSocket(int fd);
    explicit TcpSocket(int fd, const common::Address& remote_address);
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

    // Socket handler management
    virtual void SetHandler(std::shared_ptr<ISocketHandler> handler) override;
    virtual std::shared_ptr<ISocketHandler> GetHandler() const override;

private:
    int64_t fd_ = -1;
    common::Address remote_address_;

    // Socket handler
    std::weak_ptr<ISocketHandler> handler_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_TCP_SOCKET 