#ifndef QUIC_CONNECTION_CONNECTION_ID_GENERATOR
#define QUIC_CONNECTION_CONNECTION_ID_GENERATOR

#include <cstdint>

#include "common/util/singleton.h"

namespace quicx {
namespace quic {

class ConnectionIDGenerator: public common::Singleton<ConnectionIDGenerator> {
public:
    ConnectionIDGenerator();
    ~ConnectionIDGenerator();

    // Fills cid with len cryptographically random bytes. Returns false and zeroes the
    // buffer if the CSPRNG failed, so callers must not use the id on a false return.
    bool Generator(uint8_t* cid, uint32_t len);
    uint64_t Hash(uint8_t* cid, uint32_t len);

private:
    uint64_t sip_hash_key_[2];
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONNECTION_ID_GENERATOR