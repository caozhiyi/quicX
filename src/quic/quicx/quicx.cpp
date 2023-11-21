#include "quic/include/quicx.h"
#include "quic/quicx/quicx_impl.h"

namespace quicx {
namespace quic {

Quicx::Quicx() {
    _impl = new QuicxImpl();
}

Quicx::~Quicx() {
    delete _impl;
}

bool Quicx::Init(uint16_t thread_num) {
    return _impl->Init(thread_num);
}

void Quicx::Join() {
    return _impl->Join();
}

void Quicx::Destroy() {
    return _impl->Destroy();
}

bool Quicx::Connection(const std::string& ip, uint16_t port) {
    return _impl->Connection(ip, port);
}

bool Quicx::ListenAndAccept(const std::string& ip, uint16_t port) {
    return _impl->ListenAndAccept(ip, port);
}

}
}
