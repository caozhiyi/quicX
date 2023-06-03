#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "quic/connection/type.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "common/network/io_handle.h"
#include "quic/stream/crypto_stream.h"
#include "quic/packet/hand_shake_packet.h"
#include "quic/connection/client_connection.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {


ClientConnection::ClientConnection(std::shared_ptr<TLSCtx> ctx):
    BaseConnection(StreamIDGenerator::SS_CLIENT) {
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
    _tls_connection = std::make_shared<TLSClientConnection>(ctx, this);
    if (!_tls_connection->Init()) {
        LOG_ERROR("tls connection init failed.");
    }
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

    // create crypto stream
 
    return _tls_connection->DoHandleShake();
}

void ClientConnection::Close() {

}

void ClientConnection::SetHandshakeDoneCB(HandshakeDoneCB& cb) {
    _handshake_done_cb = cb;
}
bool ClientConnection::OnInitialPacket(std::shared_ptr<IPacket> packet) {
    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[packet->GetCryptoLevel()];
    // get header
    auto header = dynamic_cast<LongHeader*>(init_packet->GetHeader());
    auto buffer = std::make_shared<Buffer>(_alloter);
    buffer->Write(init_packet->GetSrcBuffer().GetStart(), init_packet->GetSrcBuffer().GetLength());
    //if(Decrypt(cryptographer, packet, buffer)) {
    //    return false;
    //}
    
    if (!init_packet->DecodeAfterDecrypt(buffer)) {
        return false;
    }
    // dispatcher frames
    OnFrames(packet->GetFrames());
    return true;
}

bool ClientConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ClientConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    auto handshake_packet = std::dynamic_pointer_cast<HandShakePacket>(packet);
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[packet->GetCryptoLevel()];
    // get header
    auto header = dynamic_cast<LongHeader*>(handshake_packet->GetHeader());
    auto buffer = std::make_shared<Buffer>(_alloter);
    buffer->Write(handshake_packet->GetSrcBuffer().GetStart(), handshake_packet->GetSrcBuffer().GetLength());
    //if(Decrypt(cryptographer, packet, buffer)) {
    //    return false;
    //}
    
    if (!handshake_packet->DecodeAfterDecrypt(buffer)) {
        return false;
    }
    // dispatcher frames
    OnFrames(packet->GetFrames());
    return true;
}

bool ClientConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ClientConnection::On1rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

void ClientConnection::MakeCryptoStream() {
    _crypto_stream = std::make_shared<CryptoStream>(_alloter);
    _crypto_stream->SetHopeSendCB(std::bind(&ClientConnection::ActiveSendStream, this, std::placeholders::_1));
    _crypto_stream->SetRecvCallBack(std::bind(&ClientConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2));
}

void ClientConnection::WriteCryptoData(std::shared_ptr<IBufferChains> buffer, int32_t err) {
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
    
    _tls_connection->DoHandleShake();
}

}