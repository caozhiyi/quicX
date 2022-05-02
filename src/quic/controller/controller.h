#ifndef QUIC_CONTROLLER_CONTROLLER
#define QUIC_CONTROLLER_CONTROLLER

#include <memory>
#include <string>
#include <cstdint>

namespace quicx {

class IPacket;
class IBufferReadOnly;
class UdpListener;
class Controller {
public:
    Controller();
    ~Controller();

    bool Listen(const std::string& ip, uint16_t port);

    bool Stop();

private:
    void Dispatcher(std::shared_ptr<IBufferReadOnly> recv_data);
    bool HandleInitial(std::shared_ptr<IPacket> packet);
    bool Handle0rtt(std::shared_ptr<IPacket> packet);
    bool HandleHandshake(std::shared_ptr<IPacket> packet);
    bool HandleRetry(std::shared_ptr<IPacket> packet);
    bool HandleNegotiation(std::shared_ptr<IPacket> packet);
    bool Handle1rtt(std::shared_ptr<IPacket> packet);

private:
    std::shared_ptr<UdpListener> _listener;
};

}

#endif