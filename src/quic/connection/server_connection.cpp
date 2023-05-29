#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "common/buffer/buffer.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/frame_interface.h"
#include "quic/packet/header/long_header.h"
#include "quic/connection/server_connection.h"
#include "quic/crypto/cryptographer_interface.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx):
    BaseConnection(StreamIDGenerator::SS_SERVER) {
    _tls_connection = std::make_shared<TLSServerConnection>(ctx, this, this);
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
}

ServerConnection::~ServerConnection() {

}

void ServerConnection::Close() {

}

bool ServerConnection::GenerateSendData(std::shared_ptr<IBuffer> buffer) {
    return true;
}

void ServerConnection::SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {

}

bool ServerConnection::OnInitialPacket(std::shared_ptr<IPacket> packet) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[packet->GetCryptoLevel()];
    // get header
    auto header = dynamic_cast<LongHeader*>(packet->GetHeader());
    if (cryptographer == nullptr) {
        // make initial cryptographer
        cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
        cryptographer->InstallInitSecret(header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(),
            __initial_slat, sizeof(__initial_slat), true);
        _cryptographers[packet->GetCryptoLevel()] = cryptographer;
    }

    auto buffer = std::make_shared<Buffer>(_alloter);
    buffer->Write(packet->GetSrcBuffer().GetStart(), packet->GetSrcBuffer().GetLength());
    //if(Decrypt(cryptographer, packet, buffer)) {
    //    return false;
    //}
    
    if (!packet->DecodeAfterDecrypt(buffer)) {
        return false;
    }
    // dispatcher frames
    OnFrames(packet->GetFrames());
    return true;
}

bool ServerConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ServerConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ServerConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool ServerConnection::On1rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

}
