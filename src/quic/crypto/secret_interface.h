#ifndef QUIC_CRYPTO_SECRET_INTERFACE
#define QUIC_CRYPTO_SECRET_INTERFACE

#include <string>

namespace quicx {

struct Secret {
    std::string _secret;
    std::string _key;
    std::string _iv;
    std::string _hp;
};

class ISecret {
public:
    ISecret() {}
    virtual ~ISecret() {}

    uint16_t GetLevel() { return _level; }
    const Secret* const ClientSecret() const { return &_client_secret; }
    const Secret* const ServerSecret() const { return &_server_secret; }

protected:
    uint16_t _level;
    Secret   _client_secret;
    Secret   _server_secret;
};

}

#endif