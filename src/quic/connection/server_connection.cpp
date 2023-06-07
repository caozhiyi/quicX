#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/connection/type.h"
#include "common/buffer/buffer.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/frame_interface.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/long_header.h"
#include "quic/connection/server_connection.h"
#include "quic/crypto/cryptographer_interface.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx):
    BaseConnection(StreamIDGenerator::SS_SERVER) {
    _tls_connection = std::make_shared<TLSServerConnection>(ctx, this, this);
    if (!_tls_connection->Init()) {
        LOG_ERROR("tls connection init failed.");
    }
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
}

ServerConnection::~ServerConnection() {

}

void ServerConnection::Close() {

}

void ServerConnection::AddTransportParam(TransportParamConfig& tp_config) {
    _transport_param.Init(tp_config);

    // set transport param. TODO define tp length
    std::shared_ptr<Buffer> buf = std::make_shared<Buffer>(_alloter);
    _transport_param.Encode(buf);
    _tls_connection->AddTransportParam(buf->GetData(), buf->GetDataLength());
}

void ServerConnection::SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    
    // parse client alpn list
    std::vector<std::string> client_protos;
    for (unsigned int i = 0; i < inlen; i++) {
        int len = in[i];
        client_protos.push_back(std::string((const char*)&in[i+1], len));
        i += len;
    }
    
    // TODO server support alpn list
    static std::vector<std::string> server_protos = {"h3", "transport"};
    
    // find a alpn
    for (auto const& client_proto : client_protos) {
        for (auto const& server_proto : server_protos) {
            if (client_proto == server_proto) {
                *out = (unsigned char*)server_proto.c_str();
                *outlen = server_proto.length();
                return;
            }
        }
    }
}

bool ServerConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ServerConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

void ServerConnection::MakeCryptoStream() {
    _crypto_stream = std::make_shared<CryptoStream>(_alloter);
    _crypto_stream->SetHopeSendCB(std::bind(&ServerConnection::ActiveSendStream, this, std::placeholders::_1));
    _crypto_stream->SetRecvCallBack(std::bind(&ServerConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2));
}

void ServerConnection::WriteCryptoData(std::shared_ptr<IBufferChains> buffer, int32_t err) {
    if (err != 0) {
        LOG_ERROR("get crypto data failed. err:%s", err);
        return;
    }
    
    uint8_t data[1450] = {0};
    uint32_t len = buffer->Read(data, 1450);
    if (!_tls_connection->ProcessCryptoData(data, len)) {
        LOG_ERROR("process crypto data failed. err:%s", err);
        return;
    }
    
    if (_tls_connection->DoHandleShake()) {
        LOG_DEBUG("handshake done.");
    }
}

}
