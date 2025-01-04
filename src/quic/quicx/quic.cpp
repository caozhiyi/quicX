#include "quic/quicx/quic.h"

namespace quicx {
namespace quic {


Quic::Quic() {

}

Quic::~Quic() {

}

bool Quic::Init(uint16_t thread_num) {
    return true;
}

bool Quic::Init(const std::string& cert_file, const std::string& key_file, uint16_t thread_num) {
    return true;
}

bool Quic::Init(const char* cert_pem, const char* key_pem, uint16_t thread_num) {
    return true;
}

void Quic::Join() {

}

void Quic::Destroy() {

}

bool Quic::Connection(const std::string& ip, uint16_t port, int32_t timeout_ms) {
    return true;
}

bool Quic::ListenAndAccept(const std::string& ip, uint16_t port) {
    return true;
}

void Quic::SetConnectionStateCallBack(connection_state_callback cb) {

}

}
}