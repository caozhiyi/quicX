#ifndef QUIC_CONNECTION_CONNECTION_INTERFACE
#define QUIC_CONNECTION_CONNECTION_INTERFACE

#include <memory>
#include <cstdint>

namespace quicx {

class Frame;
class Address;
class Connection {
public:
    Connection() {}
    virtual ~Connection() {}

    virtual bool Open(Address& addr, uint32_t strean_limit) = 0;

    virtual void Send(std::shared_ptr<Frame> frame) = 0;

    virtual void Close() = 0;
private:
    uint64_t _connection_id;
};

}

#endif