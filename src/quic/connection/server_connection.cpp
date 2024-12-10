#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/connection/type.h"
#include "common/buffer/buffer.h"
#include "quic/packet/init_packet.h"
#include "common/network/io_handle.h"
#include "quic/frame/if_frame.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/packet/header/long_header.h"
#include "quic/connection/server_connection.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(uint64_t/*cid hash*/, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t/*cid hash*/)> retire_conn_id_cb):
    BaseConnection(StreamIDGenerator::SS_SERVER, timer, add_conn_id_cb, retire_conn_id_cb) {
    _tls_connection = std::make_shared<TLSServerConnection>(ctx, &_connection_crypto, this);
    if (!_tls_connection->Init()) {
        common::LOG_ERROR("tls connection init failed.");
    }
    auto crypto_stream = std::make_shared<CryptoStream>(_alloter);
    crypto_stream->SetActiveStreamSendCB(std::bind(&ServerConnection::ActiveSendStream, this, std::placeholders::_1));
    crypto_stream->SetRecvCallBack(std::bind(&ServerConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2));

    _connection_crypto.SetCryptoStream(crypto_stream);

    auto ret = common::UdpSocket();
    if (ret._return_value < 0) {
        common::LOG_ERROR("make send socket failed. err:%d", ret.errno_);
        return;
    }
    _send_sock = ret._return_value;
}

ServerConnection::~ServerConnection() {

}

void ServerConnection::AddRemoteConnectionId(uint8_t* id, uint16_t len) {
    ConnectionID cid(id, len);
    _remote_conn_id_manager->AddID(cid);
}

void ServerConnection::AddTransportParam(TransportParamConfig& tp_config) {
    _transport_param.Init(tp_config);

    // set transport param. TODO define tp length
    std::shared_ptr<common::Buffer> buf = std::make_shared<common::Buffer>(_alloter);
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

void ServerConnection::WriteCryptoData(std::shared_ptr<common::IBufferChains> buffer, int32_t err) {
    if (err != 0) {
        common::LOG_ERROR("get crypto data failed. err:%s", err);
        return;
    }
    
    uint8_t data[1450] = {0};
    uint32_t len = buffer->Read(data, 1450);
    if (!_tls_connection->ProcessCryptoData(data, len)) {
        common::LOG_ERROR("process crypto data failed. err:%s", err);
        return;
    }
    
    if (_tls_connection->DoHandleShake()) {
        common::LOG_DEBUG("handshake done.");
    }
}

}
}