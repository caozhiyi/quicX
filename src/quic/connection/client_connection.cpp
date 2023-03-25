#include "common/log/log.h"
#include "quic/connection/type.h"
#include "common/network/io_handle.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {


ClientConnection::ClientConnection() {

}

ClientConnection::ClientConnection(TransportParamConfig& tpc) {
    _transport_param.Init(tpc);
}

ClientConnection::~ClientConnection() {
    
}

bool ClientConnection::Dial(const Address& addr) {
    // create socket
    auto ret = UdpSocket();
    if (ret.errno_ != 0) {
        LOG_ERROR("create udp socket failed.");
        return false;
    }

    // generate connection id
    char* scid[__max_cid_length] = {0};
    char* dcid[__max_cid_length] = {0};
    ConnectionIDGenerator::Instance().Generator(scid, __max_cid_length);
    ConnectionIDGenerator::Instance().Generator(dcid, __max_cid_length);
    return true;
}

void ClientConnection::Close() {

}

}