#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "quic/connection/type.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "common/network/io_handle.h"
#include "quic/stream/crypto_stream.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/connection/connection_client.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ClientConnection::ClientConnection(std::shared_ptr<TLSCtx> ctx,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t cid_hash)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    BaseConnection(StreamIDGenerator::SS_CLIENT, timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb) {
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

bool ClientConnection::Dial(const common::Address& addr, const std::string& alpn) {
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    // set application protocol
    uint8_t* alpn_data = (uint8_t*)alpn.c_str();
    if(!tls_conn->AddAlpn(alpn_data, alpn.size())) {
        common::LOG_ERROR("add alpn failed. alpn:%s", alpn.c_str());
        return false;
    }
    
    SetPeerAddress(std::move(addr));

    transport_param_.Init(TransportParamConfig::Instance());
    flow_control_->InitConfig(transport_param_);

    // set transport param. TODO define tp length
    AddTransportParam(TransportParamConfig::Instance());

    // generate connection id
    auto dcid = remote_conn_id_manager_->Generator();

    // install initial secret
    connection_crypto_.InstallInitSecret(dcid.id_, dcid.len_, false);
    
    tls_conn->DoHandleShake();
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
    const uint8_t* dcid = long_header->GetSourceConnectionId();
    uint32_t dcid_len = long_header->GetSourceConnectionIdLength();
    remote_conn_id_manager_->AddID(dcid, dcid_len);
    remote_conn_id_manager_->UseNextID();
    return OnNormalPacket(packet);
}

bool ClientConnection::OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    if (handshake_done_cb_) {
        handshake_done_cb_(shared_from_this());
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