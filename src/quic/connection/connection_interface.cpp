#include <cstring>
#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "quic/frame/crypto_frame.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/connection/connection_interface.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {

IConnection::IConnection() {
    memset(_cryptographers, 0, sizeof(std::shared_ptr<ICryptographer>) * NUM_ENCRYPTION_LEVELS);
}

IConnection::~IConnection() {

}

void IConnection::AddConnectionId(uint8_t* id, uint16_t len) {
    _conn_id_set.insert(std::string((char*)id, len));
}

void IConnection::RetireConnectionId(uint8_t* id, uint16_t len) {
    _conn_id_set.erase(std::string((char*)id, len));
}

void IConnection::SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, false);
}

void IConnection::SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, true);
}

void IConnection::WriteMessage(EncryptionLevel level, const uint8_t *data,
    size_t len) {
    
}

void IConnection::FlushFlight() {

}

void IConnection::SendAlert(EncryptionLevel level, uint8_t alert) {

}

void IConnection::HandlePacket(std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        switch (packets[i]->GetHeader()->GetPacketType())
        {
        case PT_INITIAL:
            if (!HandleInitial(std::dynamic_pointer_cast<InitPacket>(packets[i]))) {
                LOG_ERROR("init packet handle failed.");
            }
            break;
        case PT_0RTT:
            if (!Handle0rtt(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]))) {
                LOG_ERROR("0 rtt packet handle failed.");
            }
            break;
        case PT_HANDSHAKE:
            if (!HandleHandshake(std::dynamic_pointer_cast<HandShakePacket>(packets[i]))) {
                LOG_ERROR("handshakee packet handle failed.");
            }
            break;
        case PT_RETRY:
            if (!HandleRetry(std::dynamic_pointer_cast<RetryPacket>(packets[i]))) {
                LOG_ERROR("retry packet handle failed.");
            }
            break;
        case PT_1RTT:
            if (!Handle1rtt(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]))) {
                LOG_ERROR("1 rtt packet handle failed.");
            }
            break;
        default:
            LOG_ERROR("unknow packet type. type:%d", packets[i]->GetHeader()->GetPacketType());
            break;
        }
    }
}

bool IConnection::Decrypt(std::shared_ptr<ICryptographer>& cryptographer, std::shared_ptr<IPacket> packet,
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

bool IConnection::Encrypt(std::shared_ptr<ICryptographer>& cryptographer, std::shared_ptr<IPacket> packet, 
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
    //BufferSpan& plaintext, BufferSpan& sample,
    //uint8_t pn_offset, size_t pkt_number_len, bool is_short
    if(!cryptographer->EncryptHeader(header_span, sample, packet->GetPacketNumOffset(), header->GetPacketNumberLength(),
        header->GetHeaderType() == PHT_SHORT_HEADER)) {
 
        LOG_ERROR("decrypt header failed.");
        return false;
    }

    return true;
}

void IConnection::ActiveSendStream(ISendStream* stream) {
    _hope_send_stream_list.emplace_back(stream);
}

}