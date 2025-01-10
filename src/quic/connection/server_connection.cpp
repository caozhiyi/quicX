#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/frame/if_frame.h"
#include "quic/connection/type.h"
#include "common/buffer/buffer.h"
#include "quic/connection/error.h"
#include "quic/packet/init_packet.h"
#include "common/network/io_handle.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/packet/header/long_header.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx,
    const std::string& alpn,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t cid_hash)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    BaseConnection(StreamIDGenerator::SS_SERVER, timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb) {
    tls_connection_ = std::make_shared<TLSServerConnection>(ctx, &connection_crypto_, this);
    if (!tls_connection_->Init()) {
        common::LOG_ERROR("tls connection init failed.");
    }
    auto crypto_stream = std::make_shared<CryptoStream>(alloter_,
        std::bind(&ServerConnection::ActiveSendStream, this, std::placeholders::_1),
        std::bind(&ServerConnection::InnerStreamClose, this, std::placeholders::_1),
        std::bind(&ServerConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    crypto_stream->SetStreamReadCallBack(std::bind(&ServerConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2));

    connection_crypto_.SetCryptoStream(crypto_stream);
}

ServerConnection::~ServerConnection() {

}

void ServerConnection::AddRemoteConnectionId(uint8_t* id, uint16_t len) {
    ConnectionID cid(id, len);
    remote_conn_id_manager_->AddID(cid);
}

bool ServerConnection::OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    InnerConnectionClose(QUIC_ERROR_CODE::QEC_PROTOCOL_VIOLATION, 0, "server handshake done frame received");
    return true;
}

bool ServerConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    // TODO: implement server retry packet
    return true;
}

void ServerConnection::WriteCryptoData(std::shared_ptr<common::IBufferRead> buffer, int32_t err) {
    if (err != 0) {
        common::LOG_ERROR("get crypto data failed. err:%s", err);
        return;
    }
    
    // TODO do not copy data
    uint8_t data[1450] = {0};
    uint32_t len = buffer->Read(data, 1450);
    if (!tls_connection_->ProcessCryptoData(data, len)) {
        common::LOG_ERROR("process crypto data failed. err:%s", err);
        return;
    }
    
    if (tls_connection_->DoHandleShake()) {
        common::LOG_DEBUG("handshake done.");
        std::shared_ptr<HandshakeDoneFrame> frame = std::make_shared<HandshakeDoneFrame>();
        ToSendFrame(frame);
    }
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
    
    // find a alpn
    for (auto const& client_proto : client_protos) {
        if (client_proto == server_alpn_) {
            *out = (unsigned char*)server_alpn_.c_str();
            *outlen = server_alpn_.length();
            return;
        }
    }

    common::LOG_ERROR("no alpn found. server alpn:%s", server_alpn_.c_str());
    for (auto const& client_proto : client_protos) {
        common::LOG_ERROR("client alpn:%s", client_proto.c_str());
    }
}

}
}