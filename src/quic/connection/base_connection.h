#ifndef QUIC_CONNECTION_BASE_CONNECTION
#define QUIC_CONNECTION_BASE_CONNECTION

#include <set>
#include <list>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include "quic/stream/crypto_stream.h"
#include "quic/connection/transport_param.h"
#include "quic/stream/send_stream_interface.h"
#include "quic/crypto/cryptographer_interface.h"
#include "quic/connection/connection_interface.h"
#include "quic/connection/connection_flow_control.h"

namespace quicx {

class BaseConnection:
    public IConnection {
public:
    BaseConnection(StreamIDGenerator::StreamStarter start);
    virtual ~BaseConnection();

    virtual std::shared_ptr<ISendStream> MakeSendStream();
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream();

    virtual void AddConnectionId(uint8_t* id, uint16_t len);
    virtual void RetireConnectionId(uint8_t* id, uint16_t len);

    virtual void Close(uint64_t error);

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
    virtual bool OnStreamFrame(std::shared_ptr<IFrame> frame);

    virtual bool OnCryptoFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnNewTokenFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnMaxDataFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnDataBlockFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnStreamBlockFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnMaxStreamFrame(std::shared_ptr<IFrame> frame);

    virtual void ActiveSendStream(IStream* stream);

    virtual void InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string resion);
    virtual void InnerStreamClose(uint64_t stream_id);
    virtual void OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len);

    bool OnNormalPacket(std::shared_ptr<IPacket> packet);

private:
    std::shared_ptr<IStream> MakeStream(uint32_t init_size, uint64_t stream_id);

protected:
    // connection will to close
    bool _to_close;
    // last time communicate, use to idle shutdown
    uint64_t _last_communicate_time; 

    // transport param verify done
    bool _transport_param_done;
    TransportParam _transport_param;

    std::shared_ptr<CryptoStream> _crypto_stream;
    std::list<IStream*> _active_send_stream_list;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> _streams_map;

    // connection memory pool
    std::shared_ptr<BlockMemoryPool> _alloter;
    
    // connection id
    std::unordered_set<std::string> _conn_id_set;
    std::list<std::shared_ptr<IFrame>> _frames_list;

    // flow control
    ConnectionFlowControl _flow_control;
   
    // token
    std::string _token;

    // about crypto
    EncryptionLevel _cur_encryption_level;
    std::shared_ptr<ICryptographer> _cryptographers[NUM_ENCRYPTION_LEVELS];
};

}

#endif