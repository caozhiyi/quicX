#include <cstring>
#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/hand_shake_packet.h"
#include "quic/connection/base_connection.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start):
    _id_generator(start) {
    memset(_cryptographers, 0, sizeof(std::shared_ptr<ICryptographer>) * NUM_ENCRYPTION_LEVELS);
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
}

BaseConnection::~BaseConnection() {

}

void BaseConnection::AddConnectionId(uint8_t* id, uint16_t len) {
    _conn_id_set.insert(std::string((char*)id, len));
}

void BaseConnection::RetireConnectionId(uint8_t* id, uint16_t len) {
    _conn_id_set.erase(std::string((char*)id, len));
}

void BaseConnection::Close() {

}

bool BaseConnection::GenerateSendData(std::shared_ptr<IBuffer> buffer) {
    FixBufferFrameVisitor frame_visitor(1450);
    while (true) {
        if (_frame_list.empty() && _hope_send_stream_list.empty()) {
            break;
        }
        
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
            auto ret = (*iter)->TrySendData(&frame_visitor);
            if (ret == TSR_SUCCESS) {
                iter = _hope_send_stream_list.erase(iter);
    
            } else if (ret == TSR_FAILED) {
                return false;
    
            } else if (ret == TSR_BREAK) {
                break;
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

        if (!packet->Encode(buffer)) {
            return false;
        }
    }

    //uint8_t plaintext_buf[1450] = {0};
    //std::shared_ptr<IBuffer> plaintext_buffer = std::make_shared<Buffer>(plaintext_buf, plaintext_buf + 1450);
    //packet->Encode(plaintext_buffer);
    //
    //std::shared_ptr<ICryptographer> crypto_grapher = _cryptographers[GetCurEncryptionLevel()];
    //if (!Encrypt(crypto_grapher, packet, buffer)) {
    //    return false;
    //}
    return true;
}

void BaseConnection::SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, false);
}

void BaseConnection::SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, true);
}

void BaseConnection::WriteMessage(EncryptionLevel level, const uint8_t *data, size_t len) {
    if (!_crypto_stream) {
        MakeCryptoStream();
    }
    _cur_encryption_level = level;
    _crypto_stream->Send((uint8_t*)data, len, level);
}

void BaseConnection::FlushFlight() {

}

void BaseConnection::SendAlert(EncryptionLevel level, uint8_t alert) {

}

void BaseConnection::OnPackets(std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        switch (packets[i]->GetHeader()->GetPacketType())
        {
        case PT_INITIAL:
            if (!OnInitialPacket(std::dynamic_pointer_cast<InitPacket>(packets[i]))) {
                LOG_ERROR("init packet handle failed.");
            }
            break;
        case PT_0RTT:
            if (!On0rttPacket(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]))) {
                LOG_ERROR("0 rtt packet handle failed.");
            }
            break;
        case PT_HANDSHAKE:
            if (!OnHandshakePacket(std::dynamic_pointer_cast<HandShakePacket>(packets[i]))) {
                LOG_ERROR("handshakee packet handle failed.");
            }
            break;
        case PT_RETRY:
            if (!OnRetryPacket(std::dynamic_pointer_cast<RetryPacket>(packets[i]))) {
                LOG_ERROR("retry packet handle failed.");
            }
            break;
        case PT_1RTT:
            if (!On1rttPacket(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]))) {
                LOG_ERROR("1 rtt packet handle failed.");
            }
            break;
        default:
            LOG_ERROR("unknow packet type. type:%d", packets[i]->GetHeader()->GetPacketType());
            break;
        }
    }
}

bool BaseConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool BaseConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool BaseConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool BaseConnection::On1rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool BaseConnection::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames) {
    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        switch (type)
        {
        case FT_PADDING: break;
        case FT_PING: break;
        case FT_ACK: break;
        case FT_ACK_ECN: break;
        case FT_RESET_STREAM: break;
        case FT_STOP_SENDING: break;
        case FT_CRYPTO: 
            OnCryptoFrame(frames[i]);
            break;
        case FT_NEW_TOKEN: break;
        case FT_MAX_DATA: break;
        case FT_MAX_STREAM_DATA: break;
        case FT_MAX_STREAMS_BIDIRECTIONAL: break;
        case FT_MAX_STREAMS_UNIDIRECTIONAL: break;
        case FT_DATA_BLOCKED: break;
        case FT_STREAMS_BLOCKED_BIDIRECTIONAL: break;
        case FT_STREAMS_BLOCKED_UNIDIRECTIONAL: break;
        case FT_NEW_CONNECTION_ID: break;
        case FT_RETIRE_CONNECTION_ID: break;
        case FT_PATH_CHALLENGE: break;
        case FT_PATH_RESPONSE: break;
        case FT_CONNECTION_CLOSE: break;
        case FT_CONNECTION_CLOSE_APP: break;
        case FT_HANDSHAKE_DONE: break;
        default:
            if (!OnStreamFrame(std::dynamic_pointer_cast<IStreamFrame>(frames[i]))) {
                return false;
            }
            break;
        }
    }
    return true;
}

bool BaseConnection::OnStreamFrame(std::shared_ptr<IStreamFrame> frame) {
    return true;
}

bool BaseConnection::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    if (!_crypto_stream) {
        MakeCryptoStream();
    }

    _crypto_stream->OnFrame(frame);
    return true;
}

void BaseConnection::ActiveSendStream(ISendStream* stream) {
    _hope_send_stream_list.emplace_back(stream);
}

bool BaseConnection::Decrypt(std::shared_ptr<ICryptographer>& cryptographer, std::shared_ptr<IPacket> packet,
    std::shared_ptr<IBufferWrite> out_plaintext) {
    auto header = packet->GetHeader();
    // get sample
    BufferSpan head_span = header->GetHeaderSrcData();
    uint32_t packet_offset = packet->GetPacketNumOffset();
    uint8_t* pkt_number_pos = head_span.GetStart() + packet_offset;
    BufferSpan sample = BufferSpan(pkt_number_pos + 4, pkt_number_pos + 4 + __header_protect_sample_length);

    // decrypto header
    uint64_t packet_num = 0;
    uint32_t packet_num_len = 0;
    if(!cryptographer->DecryptHeader(head_span, sample, packet_offset, header->GetHeaderType() == PHT_SHORT_HEADER,
        packet_num, packet_num_len)) {

        LOG_ERROR("decrypt header failed.");
        return false;
    }

    // decrypto packet
    if(!cryptographer->DecryptPacket(packet_num, header->GetHeaderSrcData(), packet->GetSrcBuffer(), out_plaintext)) {
        LOG_ERROR("decrypt packet failed.");
        return false;
    }

    return true;
}

bool BaseConnection::Encrypt(std::shared_ptr<ICryptographer>& cryptographer, std::shared_ptr<IPacket> packet, 
    std::shared_ptr<IBuffer> out_ciphertext) {
    auto header = packet->GetHeader();

    out_ciphertext->Write(header->GetHeaderSrcData().GetStart(), header->GetHeaderSrcData().GetLength());
    auto header_span = out_ciphertext->GetReadSpan();

    // packet protection
    if(!cryptographer->EncryptPacket(packet->GetPacketNumber(), header->GetHeaderSrcData(), packet->GetSrcBuffer(), out_ciphertext)) {
        LOG_ERROR("encrypt packet failed.");
        return false;
    }

    // header protection
    uint32_t packet_offset = packet->GetPacketNumOffset();
    BufferSpan sample = BufferSpan(header_span.GetEnd() + 4,  header_span.GetEnd() + 4 + __header_protect_sample_length);
    if(!cryptographer->EncryptHeader(header_span, sample, packet->GetPacketNumOffset(), header->GetPacketNumberLength(),
        header->GetHeaderType() == PHT_SHORT_HEADER)) {
 
        LOG_ERROR("decrypt header failed.");
        return false;
    }

    return true;
}

}