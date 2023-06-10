#ifndef QUIC_CONNECTION_CONNECTION_INTERFACE
#define QUIC_CONNECTION_CONNECTION_INTERFACE

#include "quic/crypto/tls/type.h"
#include "quic/stream/send_stream.h"
#include "quic/packet/packet_interface.h"
#include "quic/crypto/tls/tls_conneciton.h"
#include "quic/frame/stream_frame_interface.h"

namespace quicx {

class IConnection:
    public TlsHandlerInterface {
public:
    IConnection() {}
    virtual ~IConnection() {}

    virtual void AddConnectionId(uint8_t* id, uint16_t len) = 0;
    virtual void RetireConnectionId(uint8_t* id, uint16_t len) = 0;

    virtual void Close() = 0;

    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<IBuffer> buffer) = 0;

    // Encryption correlation function
    virtual void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) = 0;

    virtual void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) = 0;

    virtual void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len) = 0;

    virtual void FlushFlight() = 0;
    virtual void SendAlert(EncryptionLevel level, uint8_t alert) = 0;

    virtual void OnPackets(std::vector<std::shared_ptr<IPacket>>& packets) = 0;

    virtual EncryptionLevel GetCurEncryptionLevel()  = 0;

protected:
    virtual bool OnInitialPacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool On1rttPacket(std::shared_ptr<IPacket> packet) = 0;

    virtual bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames) = 0;
    virtual bool OnStreamFrame(std::shared_ptr<IStreamFrame> frame) = 0;

    virtual void ActiveSendStream(IStream* stream) = 0;
    virtual void MakeCryptoStream() = 0;
    virtual void WriteCryptoData(std::shared_ptr<IBufferChains> buffer, int32_t err) = 0;
};

}

#endif