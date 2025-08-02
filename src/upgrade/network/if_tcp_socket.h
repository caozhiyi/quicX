#ifndef UPGRADE_NETWORK_IF_TCP_SOCKET_H
#define UPGRADE_NETWORK_IF_TCP_SOCKET_H

#include <string>
#include <vector>
#include <cstdint>

namespace quicx {
namespace upgrade {

// TCP socket interface
class ITcpSocket {
public:
    virtual ~ITcpSocket() = default;

    // Get the socket file descriptor
    virtual int GetFd() const = 0;

    // Send data
    virtual int Send(const std::vector<uint8_t>& data) = 0;
    virtual int Send(const std::string& data) = 0;

    // Receive data
    virtual int Recv(std::vector<uint8_t>& data, size_t max_size = 4096) = 0;
    virtual int Recv(std::string& data, size_t max_size = 4096) = 0;

    // Close the socket
    virtual void Close() = 0;

    // Check if socket is valid
    virtual bool IsValid() const = 0;

    // Get remote address information
    virtual std::string GetRemoteAddress() const = 0;
    virtual uint16_t GetRemotePort() const = 0;

    // Get local address information
    virtual std::string GetLocalAddress() const = 0;
    virtual uint16_t GetLocalPort() const = 0;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_IF_TCP_SOCKET_H 