#ifndef QUIC_CONNECTION_BASE_CONNECTION
#define QUIC_CONNECTION_BASE_CONNECTION

#include <set>
#include <list>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include "quic/include/type.h"
#include "quic/connection/type.h"
#include "quic/connection/if_connection.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/connection_crypto.h"
#include "quic/connection/controler/flow_control.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/controler/recv_control.h"

namespace quicx {
namespace quic {

class BaseConnection:
    public IConnection,
    public std::enable_shared_from_this<BaseConnection> {
public:
    BaseConnection(StreamIDGenerator::StreamStarter start,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t cid_hash)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb);
    virtual ~BaseConnection();
    //*************** outside interface ***************//
    virtual void Close();
    virtual void Reset(uint32_t error_code);
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type);

    // *************** inner interface ***************//
    // set transport param
    void AddTransportParam(TransportParamConfig& tp_config);
    virtual uint64_t GetConnectionIDHash();
    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_operation);
    // handle packets
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets);
    virtual EncryptionLevel GetCurEncryptionLevel() { return connection_crypto_.GetCurEncryptionLevel(); }

protected:
    bool OnInitialPacket(std::shared_ptr<IPacket> packet);
    bool On0rttPacket(std::shared_ptr<IPacket> packet);
    bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
    bool On1rttPacket(std::shared_ptr<IPacket> packet);
    bool OnNormalPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) = 0;

    // handle frames
    bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);
    bool OnStreamFrame(std::shared_ptr<IFrame> frame);
    bool OnAckFrame(std::shared_ptr<IFrame> frame, uint16_t crypto_level);
    bool OnCryptoFrame(std::shared_ptr<IFrame> frame);
    bool OnNewTokenFrame(std::shared_ptr<IFrame> frame);
    bool OnMaxDataFrame(std::shared_ptr<IFrame> frame);
    bool OnDataBlockFrame(std::shared_ptr<IFrame> frame);
    bool OnStreamBlockFrame(std::shared_ptr<IFrame> frame);
    bool OnMaxStreamFrame(std::shared_ptr<IFrame> frame);
    bool OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame);
    bool OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame);
    bool OnConnectionCloseFrame(std::shared_ptr<IFrame> frame);
    bool OnConnectionCloseAppFrame(std::shared_ptr<IFrame> frame);
    bool OnPathChallengeFrame(std::shared_ptr<IFrame> frame);
    bool OnPathResponseFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame) = 0;

    void OnTransportParams(TransportParam& remote_tp);

protected:
    void ToSendFrame(std::shared_ptr<IFrame> frame);
    void ActiveSendStream(std::shared_ptr<IStream> stream);
    void ActiveSend();

    void InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string resion);
    void InnerStreamClose(uint64_t stream_id);

    void AddConnectionId(uint64_t cid_hash);
    void RetireConnectionId(uint64_t cid_hash);
    
    std::shared_ptr<IStream> MakeStream(uint32_t init_size, uint64_t stream_id, StreamDirection sd);

    virtual void WriteCryptoData(std::shared_ptr<common::IBufferRead> buffer, int32_t err) = 0;

protected:
    // transport param verify done
    TransportParam transport_param_;
    // connection memory pool
    std::shared_ptr<common::BlockMemoryPool> alloter_;
    // connection will to close
    bool to_close_;
    // last time communicate, use to idle shutdown
    uint64_t last_communicate_time_; 
    // streams
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_map_;
    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;
    // flow control
    std::shared_ptr<FlowControl> flow_control_;
    RecvControl recv_control_;
    SendManager send_manager_;
    // crypto
    ConnectionCrypto connection_crypto_;
    // token
    std::string token_;
    std::shared_ptr<TLSConnection> tls_connection_;
};

}
}

#endif