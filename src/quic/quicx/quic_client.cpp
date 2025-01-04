#include "quic/quicx/quic_client.h"

namespace quicx {
namespace quic {

QuicClient::QuicClient() {

}

QuicClient::~QuicClient() {

}

bool QuicClient::Init(uint16_t thread_num) {
    return true;
}

void QuicClient::Join() {

}

void QuicClient::Destroy() {

}

bool QuicClient::Connection(const std::string& ip, uint16_t port, int32_t timeout_ms) {
    return true;
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    
}

}
}