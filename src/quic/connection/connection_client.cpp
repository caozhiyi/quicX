#include "common/log/log.h"
#include "quic/stream/crypto_stream.h"
#include "quic/packet/handshake_packet.h"
#include "quic/connection/session_cache.h"
#include "quic/connection/connection_client.h"


namespace quicx {
namespace quic {

ClientConnection::ClientConnection(std::shared_ptr<TLSCtx> ctx,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(ConnectionID&)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    BaseConnection(StreamIDGenerator::StreamStarter::kClient, false, timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb) {
    tls_connection_ = std::make_shared<TLSClientConnection>(ctx, &connection_crypto_);
    if (!tls_connection_->Init()) {
        common::LOG_ERROR("tls connection init failed.");
    }

    auto crypto_stream = std::make_shared<CryptoStream>(alloter_,
        std::bind(&ClientConnection::ActiveSendStream, this, std::placeholders::_1),
        std::bind(&ClientConnection::InnerStreamClose, this, std::placeholders::_1),
        std::bind(&ClientConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    crypto_stream->SetStreamReadCallBack(std::bind(&ClientConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2));

    connection_crypto_.SetCryptoStream(crypto_stream);
}

ClientConnection::~ClientConnection() {
    
}

bool ClientConnection::Dial(const common::Address& addr, const std::string& alpn, const QuicTransportParams& tp_config) {
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    // set application protocol
    uint8_t* alpn_data = (uint8_t*)alpn.c_str();
    if(!tls_conn->AddAlpn(alpn_data, alpn.size())) {
        common::LOG_ERROR("add alpn failed. alpn:%s", alpn.c_str());
        return false;
    }

    SetPeerAddress(std::move(addr));

    // set transport param. TODO define tp length
    AddTransportParam(tp_config);

    std::string session_der;
    if (SessionCache::Instance().GetSession(GetPeerAddress().AsString(), session_der)) {
        if (!tls_conn->SetSession(reinterpret_cast<const uint8_t*>(session_der.data()), session_der.size())) {
            common::LOG_ERROR("set session failed. session_der:%s", session_der.c_str());
            return false;
        }
    }

    // generate connection id
    auto dcid = remote_conn_id_manager_->Generator();

    // install initial secret
    connection_crypto_.InstallInitSecret(dcid.GetID(), dcid.GetLength(), false);
    
    tls_conn->DoHandleShake();
    return true;
}

bool ClientConnection::Dial(const common::Address& addr, const std::string& alpn, const std::string& resumption_session_der, const QuicTransportParams& tp_config) {
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    // set application protocol
    uint8_t* alpn_data = (uint8_t*)alpn.c_str();
    if(!tls_conn->AddAlpn(alpn_data, alpn.size())) {
        common::LOG_ERROR("add alpn failed. alpn:%s", alpn.c_str());
        return false;
    }

    // set transport param. TODO define tp length
    AddTransportParam(tp_config);

    if (!tls_conn->SetSession(reinterpret_cast<const uint8_t*>(resumption_session_der.data()), resumption_session_der.size())) {
        common::LOG_ERROR("set session failed. session_der:%s", resumption_session_der.c_str());
        return false;
    }

    SetPeerAddress(std::move(addr));

    // generate connection id
    auto dcid = remote_conn_id_manager_->Generator();

    // install initial secret
    connection_crypto_.InstallInitSecret(dcid.GetID(), dcid.GetLength(), false);
    
    tls_conn->DoHandleShake();
    return true;
}

bool ClientConnection::ExportResumptionSession(std::string& out_session_der) {
    auto client_tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    SessionInfo session_info;
    if (!client_tls_conn->ExportSession(out_session_der, session_info)) {
        common::LOG_ERROR("export session failed.");
        return false;
    }

    return true;
}

bool ClientConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    auto handshake_packet = std::dynamic_pointer_cast<HandshakePacket>(packet);
    if (!handshake_packet) {
        common::LOG_ERROR("packet type is not handshake packet.");
        return false;
    }

    // client side should update remote connection id here
    auto long_header = static_cast<LongHeader*>(handshake_packet->GetHeader());
    remote_conn_id_manager_->AddID(long_header->GetSourceConnectionId(), long_header->GetSourceConnectionIdLength());
    remote_conn_id_manager_->UseNextID();
    return OnNormalPacket(packet);
}

bool ClientConnection::OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    state_ = ConnectionStateType::kStateConnected;
    if (handshake_done_cb_) {
        handshake_done_cb_(shared_from_this());
    }

    if (SessionCache::Instance().IsEnableSessionCache()) {
        SessionInfo session_info;
        std::string session_der;
        auto client_tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
        if (client_tls_conn->ExportSession(session_der, session_info)) {
            SessionCache::Instance().StoreSession(session_der, session_info);
        }
    }
    return true;
}

bool ClientConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

void ClientConnection::WriteCryptoData(std::shared_ptr<common::IBufferRead> buffer, int32_t err) {
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
    }
}

}
}