#ifndef QUIC_INCLUDE_QUICX
#define QUIC_INCLUDE_QUICX

#include <cstdint>
#include <string>
#include "quic/include/type.h"
namespace quicx {
namespace quic {

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

    virtual void SetConnectionStateCallBack(connection_state_call_back cb) {}
private:
    QuicxImpl* _impl;
};

}
}

#endif