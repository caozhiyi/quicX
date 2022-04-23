#ifndef QUIC_CONTROLLER_CONTROLLER
#define QUIC_CONTROLLER_CONTROLLER

#include <memory>
#include <string>
#include <cstdint>

namespace quicx {

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

private:
    std::shared_ptr<UdpListener> _listener;
};

}

#endif