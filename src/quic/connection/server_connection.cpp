#include "openssl/ssl.h"

#include "common/log/log.h"
#include "quic/packet/long_header.h"
#include "quic/packet/init_packet.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx):
    _tls_connection(ctx, shared_from_this(), shared_from_this()) {

}

ServerConnection::~ServerConnection() {

}

void ServerConnection::Close() {

}

void ServerConnection::SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {

}

bool ServerConnection::HandleInitial(std::shared_ptr<InitPacket> packet) {
    return true;
}

bool ServerConnection::Handle0rtt(std::shared_ptr<Rtt0Packet> packet) {
    return true;
}

bool ServerConnection::HandleHandshake(std::shared_ptr<HandShakePacket> packet) {
    return true;
}

bool ServerConnection::HandleRetry(std::shared_ptr<RetryPacket> packet) {
    return true;
}

bool ServerConnection::Handle1rtt(std::shared_ptr<Rtt1Packet> packet) {
    return true;
}

}
