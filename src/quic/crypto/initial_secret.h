#ifndef QUIC_CRYPTO_INITIAL_SECRET
#define QUIC_CRYPTO_INITIAL_SECRET

#include <string>
#include <cstdint>
#include "quic/crypto/secret_interface.h"

namespace quicx {

class InitialSecret:
    public ISecret {
public:
    InitialSecret();
    virtual ~InitialSecret();

    bool Generate(char* sercet, uint16_t length);
};

}

#endif