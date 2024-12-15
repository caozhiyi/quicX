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
    std::function<void(uint64_t/*cid hash*/, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t/*cid hash*/)> retire_conn_id_cb):
    BaseConnection(StreamIDGenerator::SS_CLIENT, timer, add_conn_id_cb, retire_conn_id_cb) {
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

void ClientConnection::AddTransportParam(TransportParamConfig& tp_config) {
    transport_param_.Init(tp_config);
}

bool ClientConnection::Dial(const common::Address& addr) {
    // set application protocol
    if (alpn_type_ == AT_HTTP3) {
        if(!tls_connection_->AddAlpn(__alpn_h3, 2)) {
            common::LOG_ERROR("add alpn failed. alpn:%s", __alpn_h3);
            return false;
        }
    }

    // set transport param. TODO define tp length
    std::shared_ptr<common::Buffer> buf = std::make_shared<common::Buffer>(alloter_);
    
    transport_param_.Encode(buf);
    tls_connection_->AddTransportParam(buf->GetData(), buf->GetDataLength());

    // create socket
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed.");
        return false;
    }

    send_sock_ = ret.return_value_;

    flow_control_->InitConfig(transport_param_);

    // generate connection id
    auto dcid = remote_conn_id_manager_->Generator();

    // install initial secret
    connection_crypto_.InstallInitSecret(dcid.id_, dcid.len_, false);
    
    tls_connection_->DoHandleShake();
    return true;
}

void ClientConnection::SetHandshakeDoneCB(HandshakeDoneCB& cb) {
    handshake_done_cb_ = cb;
}

bool ClientConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ClientConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

void ClientConnection::WriteCryptoData(std::shared_ptr<common::IBufferChains> buffer, int32_t err) {
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