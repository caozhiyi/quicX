#ifndef QUIC_TRANSPORT_CONNECTION_INTERFACE
#define QUIC_TRANSPORT_CONNECTION_INTERFACE

#include <memory>
#include <cstdint>

namespace quicx {

class Frame;
class Connection {
public:
    Connection() {}
    virtual ~Connection() {}

    virtual void Send(std::shared_ptr<Frame> frame) = 0;
private:
    uint64_t _connection_id;
};

}

#endif