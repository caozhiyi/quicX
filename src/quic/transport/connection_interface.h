#ifndef QUIC_TRANSPORT_CONNECTION_INTERFACE
#define QUIC_TRANSPORT_CONNECTION_INTERFACE

#include <cstdint>

namespace quicx {

class Connection {
public:
    Connection();
    ~Connection();

private:
    uint64_t _connection_id;

};

}

#endif