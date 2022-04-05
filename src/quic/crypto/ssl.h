#ifndef QUIC_CRYPTO_SSL
#define QUIC_CRYPTO_SSL

#include "openssl/ssl.h"

namespace quicx {

class QuicSsl {


private:
    SSL *_ssl;
};


}

#endif