#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <cstdint>
#include "common/util/singleton.h"

namespace quicx {

class ConnectionIDGenerator:
    public Singleton<ConnectionIDGenerator> {
public:
    ConnectionIDGenerator();
    ~ConnectionIDGenerator();
    
    void Generator(void* cid, uint32_t len);
    uint64_t Hash(uint8_t* cid, uint16_t len);

private: 
    uint64_t _sip_hash_key[2];    
};

}

#endif