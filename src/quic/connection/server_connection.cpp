#include <openssl/tls1.h>
#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/header/long_header.h"
#include "quic/connection/server_connection.h"
#include "quic/crypto/cryptographer_interface.h"
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
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[packet->GetCryptoLevel()];
    if (cryptographer != nullptr) {
        LOG_ERROR("aleady recv initial packet.");
        return false;
    }

    // get header
    auto header = dynamic_cast<LongHeader*>(packet->GetHeader());

    // make initial cryptographer
    cryptographer = MakeCryptographer(TLS1_CK_AES_128_GCM_SHA256);
    cryptographer->InstallInitSecret(header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(),
        __initial_slat, sizeof(__initial_slat), true);

    //cryptographer->DecryptHeader();    
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
