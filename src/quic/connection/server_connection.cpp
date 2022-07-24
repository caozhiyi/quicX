#include "openssl/ssl.h"

#include "common/log/log.h"
#include "quic/packet/long_header.h"
#include "quic/packet/init_packet.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {

ServerConnection::ServerConnection() {

}

ServerConnection::~ServerConnection() {

}

bool ServerConnection::Init(char* init_kay, uint16_t init_len) {
    // init transport param
    _transport_param.Init(TransportParamConfig::Instance());

    // create crypto init secret
    return _ssl_connection.MakeInitSecret(init_kay, init_len);
}

bool ServerConnection::HandleInitPacket(std::shared_ptr<InitPacket> packet) {
    std::shared_ptr<LongHeader> header = std::dynamic_pointer_cast<LongHeader>(packet->GetHeader());
    if (!header) {
        LOG_ERROR("dynamic long header failed.");
        return false;
    }
    
    // check destination connection id length
    if (header->GetDestinationConnectionIdLength() < __min_connection_length) {
        LOG_ERROR("quic too short dcid in initial. len:%d", header->GetDestinationConnectionIdLength());
        return false;
    }

    // check secret valid
    if(!_ssl_connection.IsAvailableKey(ssl_encryption_initial)) {
        LOG_ERROR("encryption initial secret is invalid.");
        return false;
    }

    return true;
}   

}
