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
    ConnectionID(const uint8_t* id, uint8_t len, uint64_t sequence_number = 0);
    ConnectionID(const ConnectionID& other);
    ~ConnectionID();

    uint64_t Hash();

    const uint8_t* GetID() const;
    uint8_t GetLength() const;
    void SetID(uint8_t* id, uint8_t len);

    uint64_t GetSequenceNumber() const;
    void SetSequenceNumber(uint64_t sequence_number) { sequence_number_ = sequence_number; }

    void operator=(const ConnectionID& other);
    bool operator==(const ConnectionID& other) const;
    bool operator!=(const ConnectionID& other) const;

private:
    friend class ConnectionIDManager;

    uint8_t id_[kMaxCidLength];
    uint8_t length_;
    uint64_t sequence_number_;
    mutable uint64_t hash_; // mutable for lazy initialization in Hash()
};


}
}

#endif