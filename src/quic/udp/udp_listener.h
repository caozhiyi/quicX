#ifndef QUIC_UDP_LISTENER
#define QUIC_UDP_LISTENER

#include <memory>
#include <string>
#include <cstdint>
#include <functional>

namespace quicx {

class IBufferRead;
class UdpListener {
public:
    UdpListener(std::function<void(std::shared_ptr<IBufferRead>)> cb);
    ~UdpListener();

    bool Listen(const std::string& ip, uint16_t port);

    bool Stop();

private:
    bool _stop;
    uint64_t _listen_sock;
    std::function<void(std::shared_ptr<IBufferRead>)> _recv_callback;
};

}

#endif