#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/frame/if_frame.h"
#include "quic/connection/type.h"
#include "common/buffer/buffer.h"
#include "quic/packet/init_packet.h"
#include "common/network/io_handle.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/packet/header/long_header.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t cid_hash)> retire_conn_id_cb):
    BaseConnection(StreamIDGenerator::SS_SERVER, timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb) {
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

bool ServerConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

}
}