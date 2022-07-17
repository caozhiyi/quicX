#ifndef QUIC_CRYPTO_CRYPTER_INTERFACE
#define QUIC_CRYPTO_CRYPTER_INTERFACE

#include <string>

class CrypterIntreface {
public:
    virtual ~CrypterIntreface() {}

    virtual void SetSecret(const std::string& serret) = 0;

    virtual void SetIV(const std::string& iv) = 0;

    virtual void SetHeaderSecret(const std::string& secret) = 0;
};

#endif