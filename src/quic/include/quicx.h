#ifndef QUIC_INCLUDE_QUICX
#define QUIC_INCLUDE_QUICX

#include <stdint.h>
#include <string>

namespace quicx {

class QuicxImpl;
class Quicx {
public:
    Quicx();
    virtual ~Quicx();

    virtual bool Init(uint16_t thread_num);
    virtual void Join();
    virtual void Destroy();

    virtual bool Connection(const std::string& ip, uint16_t port);
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port);
private:
    QuicxImpl* _impl;
};

}

#endif