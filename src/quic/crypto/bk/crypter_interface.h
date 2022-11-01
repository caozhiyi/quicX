#ifndef QUIC_CRYPTO_CRYPTER_INTERFACE
#define QUIC_CRYPTO_CRYPTER_INTERFACE

#include <string>

namespace quicx {

class CrypterIntreface {
public:
    CrypterIntreface() {}
    virtual ~CrypterIntreface() {}

    virtual bool SetSecret(const std::string& serret) = 0;

    virtual bool SetIV(const std::string& iv) = 0;

    virtual bool SetHeaderSecret(const std::string& secret) = 0;
};

}

#endif