#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "quic/connection/type.h"
#include "common/network/io_handle.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {


ClientConnection::ClientConnection(std::shared_ptr<TLSCtx> ctx) {
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
    _tls_connection = std::make_shared<TLSClientConnection>(ctx, this);
}

ClientConnection::~ClientConnection() {
    
}

void ClientConnection::AddAlpn(AlpnType at) {
    _alpn_type = at;
}

void ClientConnection::AddTransportParam(TransportParamConfig& tp_config) {
    _transport_param.Init(tp_config);
}

bool ClientConnection::Dial(const Address& addr) {
    _tls_connection->Init();

    // set application protocol
    if (_alpn_type == AT_HTTP3) {
        if(!_tls_connection->AddAlpn(__alpn_h3, 2)) {
            LOG_ERROR("add alpn failed. alpn:%s", __alpn_h3);
            return false;
        }
    }

    // set transport param. TODO define tp length
    std::shared_ptr<Buffer> buf = std::make_shared<Buffer>(_alloter);
    
    _transport_param.Encode(buf);
    _tls_connection->AddTransportParam(buf->GetData(), buf->GetDataLength());

    // create socket
    auto ret = UdpSocket();
    if (ret.errno_ != 0) {
        LOG_ERROR("create udp socket failed.");
        return false;
    }

    // generate connection id
    uint8_t scid[__max_cid_length] = {0};
    uint8_t dcid[__max_cid_length] = {0};
    ConnectionIDGenerator::Instance().Generator(scid, __max_cid_length);
    ConnectionIDGenerator::Instance().Generator(dcid, __max_cid_length);

    // install initial secret
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[PCL_INITIAL];
    // get header
    if (cryptographer == nullptr) {
        // make initial cryptographer
        cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
        cryptographer->InstallInitSecret(dcid, __max_cid_length,
            __initial_slat, sizeof(__initial_slat), false);
        _cryptographers[PCL_INITIAL] = cryptographer;
    }

    return _tls_connection->DoHandleShake();
}

void ClientConnection::Close() {

}

bool ClientConnection::HandleInitial(std::shared_ptr<InitPacket> packet) {
    return true;
}

bool ClientConnection::Handle0rtt(std::shared_ptr<Rtt0Packet> packet) {
    return true;
}

bool ClientConnection::HandleHandshake(std::shared_ptr<HandShakePacket> packet) {
    return true;
}

bool ClientConnection::HandleRetry(std::shared_ptr<RetryPacket> packet) {
    return true;
}

bool ClientConnection::Handle1rtt(std::shared_ptr<Rtt1Packet> packet) {
    return true;
}

}