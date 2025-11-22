#include "common/log/log.h"
#include "quic/frame/if_frame.h"
#include "quic/connection/error.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/connection/connection_server.h"

namespace quicx {
namespace quic {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx,
    const std::string& alpn,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(ConnectionID&)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    BaseConnection(StreamIDGenerator::StreamStarter::kServer, false, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb),
    server_alpn_(alpn) {
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

void ServerConnection::AddRemoteConnectionId(ConnectionID& id) {
    remote_conn_id_manager_->AddID(id);
}

bool ServerConnection::OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    InnerConnectionClose(QuicErrorCode::kProtocolViolation, 0, "server handshake done frame received");
    return true;
}

bool ServerConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    // TODO: implement server retry packet
    return true;
}

void ServerConnection::WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err) {
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

        state_ = ConnectionStateType::kStateConnected;
        // notify handshake done
        if (handshake_done_cb_) {
            handshake_done_cb_(shared_from_this());
        }
    }
}

void ServerConnection::SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    
    // parse client alpn list
    // ALPN format: [length1][protocol1][length2][protocol2]...
    std::vector<std::string> client_protos;
    for (unsigned int i = 0; i < inlen;) {
        if (i >= inlen) {
            break;
        }
        unsigned char len = in[i++];
        if (i + len > inlen) {
            common::LOG_ERROR("invalid ALPN format: length %d exceeds remaining data", len);
            break;
        }
        std::string proto((const char*)&in[i], len);
        client_protos.push_back(proto);
        common::LOG_DEBUG("client alpn:%s", proto.c_str());
        i += len;
    }
    common::LOG_DEBUG("server alpn:%s", server_alpn_.c_str());

    // find a matching alpn
    for (auto const& client_proto : client_protos) {
        if (client_proto == server_alpn_) {
            *out = (unsigned char*)server_alpn_.c_str();
            *outlen = server_alpn_.length();
            common::LOG_DEBUG("ALPN selected: %s", server_alpn_.c_str());
            return;
        }
    }

    common::LOG_ERROR("no alpn found. server alpn:%s (len:%d)", server_alpn_.c_str(), server_alpn_.length());
    for (size_t i = 0; i < server_alpn_.length(); ++i) {
        common::LOG_ERROR("server alpn[%d]: 0x%02x", i, (unsigned char)server_alpn_[i]);
    }

    for (auto const& client_proto : client_protos) {
        common::LOG_ERROR("client alpn:%s (len:%d)", client_proto.c_str(), client_proto.length());
        for (size_t i = 0; i < client_proto.length(); ++i) {
            common::LOG_ERROR("client alpn[%d]: 0x%02x", i, (unsigned char)client_proto[i]);
        }
    }
}

}
}