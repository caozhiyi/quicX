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
    _id_generator(StreamIDGenerator::SS_CLIENT) {
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
    _tls_connection = std::make_shared<TLSClientConnection>(ctx, this);
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
    _tls_connection->Init();

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
    _crypto_stream = std::make_shared<CryptoStream>(_alloter, _id_generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL));
    _crypto_stream->SetHopeSendCB(std::bind(&ClientConnection::ActiveSendStream, this, std::placeholders::_1));
    return _tls_connection->DoHandleShake();
}

void ClientConnection::Close() {

}

bool ClientConnection::TrySendData(IPacketVisitor* pkt_visitor) {
    FixBufferFrameVisitor frame_visitor(1450);
    // priority sending frames of connection
    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (frame_visitor.HandleFrame(*iter)) {
            iter = _frame_list.erase(iter);

        } else {
            return false;
        }
    }

    // then sending frames of stream
    for (auto iter = _hope_send_stream_list.begin(); iter != _hope_send_stream_list.end();) {
        if((*iter)->TrySendData(&frame_visitor)) {
            iter = _hope_send_stream_list.erase(iter);
        } else {
            return false;
        }
    }

    // make quic packet
    std::shared_ptr<IPacket> packet;
    switch (GetCurEncryptionLevel()) {
    case EL_INITIAL: {
        auto init_packet = std::make_shared<InitPacket>();
        init_packet->SetPayload(frame_visitor.GetBuffer()->GetReadSpan());
        packet = init_packet;
        break;
    }
    case EL_EARLY_DATA: {
        packet = std::make_shared<Rtt0Packet>();
        break;
    }
    case EL_HANDSHAKE: {
        packet = std::make_shared<HandShakePacket>();
        break;
    }
    case EL_APPLICATION: {
        packet = std::make_shared<Rtt1Packet>();
        break;
    }
    }

    pkt_visitor->HandlePacket(packet);
    return true;
}

void ClientConnection::SetHandshakeDoneCB(HandshakeDoneCB& cb) {
    _handshake_done_cb = cb;
}

bool ClientConnection::HandleInitial(std::shared_ptr<InitPacket> packet) {
    return true;
}

bool ClientConnection::Handle0rtt(std::shared_ptr<Rtt0Packet> packet) {
    return true;
}

bool ClientConnection::HandleHandshake(std::shared_ptr<HandShakePacket> packet) {
    return true;
}

bool ClientConnection::HandleRetry(std::shared_ptr<RetryPacket> packet) {
    return true;
}

bool ClientConnection::Handle1rtt(std::shared_ptr<Rtt1Packet> packet) {
    return true;
}

void ClientConnection::WriteMessage(EncryptionLevel level, const uint8_t *data, size_t len) {
    if (!_crypto_stream) {
        _crypto_stream = std::make_shared<CryptoStream>(_alloter, _id_generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL));
    }
    
    _crypto_stream->Send((uint8_t*)data, len);
}

}