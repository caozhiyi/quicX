#include "quic/quicx/quic_server.h"

namespace quicx {
namespace quic {


QuicServer::QuicServer() {

}

QuicServer::~QuicServer() {

}

bool QuicServer::Init(const std::string& cert_file, const std::string& key_file, uint16_t thread_num) {
    return true;
}

bool QuicServer::Init(const char* cert_pem, const char* key_pem, uint16_t thread_num) {
    return true;
}

void QuicServer::Join() {

}

void QuicServer::Destroy() {

}

bool QuicServer::ListenAndAccept(const std::string& ip, uint16_t port) {
    return true;
}

void QuicServer::SetConnectionStateCallBack(connection_state_callback cb) {

}

}
}