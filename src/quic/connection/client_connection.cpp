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
#include "quic/connection/client_connection.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {


ClientConnection::ClientConnection(std::shared_ptr<TLSCtx> ctx,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t cid_hash)> retire_conn_id_cb):
    BaseConnection(StreamIDGenerator::SS_CLIENT, timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb) {
    tls_connection_ = std::make_shared<TLSClientConnection>(ctx, &connection_crypto_);
    if (!tls_connection_->Init()) {
        common::LOG_ERROR("tls connection init failed.");
    }

    auto crypto_stream = std::make_shared<CryptoStream>(alloter_);
    crypto_stream->SetActiveStreamSendCB(std::bind(&ClientConnection::ActiveSendStream, this, std::placeholders::_1));
    crypto_stream->SetRecvCallBack(std::bind(&ClientConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2));

    connection_crypto_.SetCryptoStream(crypto_stream);
}

ClientConnection::~ClientConnection() {
    
}

void ClientConnection::AddAlpn(AlpnType at) {
    alpn_type_ = at;
}

bool ClientConnection::Dial(const common::Address& addr) {
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    // set application protocol
    if (alpn_type_ == AT_HTTP3) {
        if(!tls_conn->AddAlpn(__alpn_h3, 2)) {
            common::LOG_ERROR("add alpn failed. alpn:%s", __alpn_h3);
            return false;
        }
    }

    // set transport param. TODO define tp length
    std::shared_ptr<common::Buffer> buf = std::make_shared<common::Buffer>(alloter_);
    
    transport_param_.Encode(buf);
    tls_conn->AddTransportParam(buf->GetData(), buf->GetDataLength());

    flow_control_->InitConfig(transport_param_);

    // generate connection id
    auto dcid = remote_conn_id_manager_->Generator();

    // install initial secret
    connection_crypto_.InstallInitSecret(dcid.id_, dcid.len_, false);
    
    tls_conn->DoHandleShake();
    return true;
}

bool ClientConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

}
}