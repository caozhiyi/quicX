#ifndef QUIC_CONNECTION_BASE_CONNECTION
#define QUIC_CONNECTION_BASE_CONNECTION

#include <list>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include "quic/stream/crypto_stream.h"
#include "quic/stream/stream_id_generator.h"
#include "quic/connection/transport_param.h"
#include "quic/stream/send_stream_interface.h"
#include "quic/crypto/cryptographer_interface.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class BaseConnection:
    public IConnection {
public:
    BaseConnection(StreamIDGenerator::StreamStarter start);
    virtual ~BaseConnection();

    void AddConnectionId(uint8_t* id, uint16_t len);
    void RetireConnectionId(uint8_t* id, uint16_t len);

    virtual void Close();

    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<IBuffer> buffer);

    // Encryption correlation function
    virtual void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

    virtual void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

    virtual void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len);

    virtual void FlushFlight();
    virtual void SendAlert(EncryptionLevel level, uint8_t alert);

    virtual void OnPackets(std::vector<std::shared_ptr<IPacket>>& packets);

    virtual EncryptionLevel GetCurEncryptionLevel() { return _cur_encryption_level; }

protected:
    virtual bool OnInitialPacket(std::shared_ptr<IPacket> packet);
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);
    virtual bool On1rttPacket(std::shared_ptr<IPacket> packet);

    virtual bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames);
    virtual bool OnStreamFrame(std::shared_ptr<IStreamFrame> frame);
    virtual bool OnCryptoFrame(std::shared_ptr<IFrame> frame);

    virtual void ActiveSendStream(ISendStream* stream);

    bool OnNormalPacket(std::shared_ptr<IPacket> packet);

protected:
    std::shared_ptr<BlockMemoryPool> _alloter;

    StreamIDGenerator _id_generator;
    std::shared_ptr<CryptoStream> _crypto_stream;

    TransportParam _transport_param;
    std::unordered_set<std::string> _conn_id_set;
    std::list<std::shared_ptr<IFrame>> _frame_list;

    std::list<ISendStream*> _hope_send_stream_list;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> _stream_map;

    EncryptionLevel _cur_encryption_level;
    std::shared_ptr<ICryptographer> _cryptographers[NUM_ENCRYPTION_LEVELS];  
};

}

#endif