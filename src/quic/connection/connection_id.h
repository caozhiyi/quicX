#ifndef QUIC_CONNECTION_CONNECTION_ID
#define QUIC_CONNECTION_CONNECTION_ID

#include <cstdint>
#include <cstring>

#include "quic/connection/type.h"

namespace quicx {
namespace quic {

class ConnectionID {
public:
    ConnectionID();
    ConnectionID(uint8_t* id, uint8_t len, uint64_t sequence_number = 0);
    ConnectionID(const ConnectionID& other);
    ~ConnectionID();

    uint64_t Hash();
    uint64_t SequenceNumber() const;
    const uint8_t* ID() const;
    uint8_t Len() const;

    void SetID(uint8_t* id, uint8_t len);
    void SetSequenceNumber(uint64_t sequence_number) { sequence_number_ = sequence_number; }

    void operator=(const ConnectionID& other);
    bool operator==(const ConnectionID& other) const;
    bool operator!=(const ConnectionID& other) const;

private:
    friend class ConnectionIDManager;

    uint8_t id_[kMaxCidLength];
    uint8_t len_;
    uint64_t sequence_number_;
    uint64_t hash_;
};


}
}

#endif