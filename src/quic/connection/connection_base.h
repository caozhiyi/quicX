#ifndef QUIC_CONNECTION_CONNECTION_BASE
#define QUIC_CONNECTION_CONNECTION_BASE

// #include <set>
// #include <list>
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
        bool ecn_enabled,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb);
    virtual ~BaseConnection();
    //*************** outside interface ***************//
    virtual void Close() override;
    virtual void Reset(uint32_t error_code) override;
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) override;

    // *************** inner interface ***************//
    // set transport param
    void AddTransportParam(const QuicTransportParams& tp_config) override;
    virtual uint64_t GetConnectionIDHash() override;
    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_operation) override;
    // handle packets
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) override;
    virtual void SetPendingEcn(uint8_t ecn) override { pending_ecn_ = ecn; }
    virtual EncryptionLevel GetCurEncryptionLevel() override;

    // observed peer address from network; store as candidate if different
    virtual void OnObservedPeerAddress(const common::Address& addr) override;

protected:
    bool OnInitialPacket(std::shared_ptr<IPacket> packet);
    bool On0rttPacket(std::shared_ptr<IPacket> packet);
    bool On1rttPacket(std::shared_ptr<IPacket> packet);
    bool OnNormalPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
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
    virtual void ThreadTransferBefore() override; 
    virtual void ThreadTransferAfter() override;
    // idle timeout
    void OnIdleTimeout();
    void OnClosingTimeout();
    uint32_t GetCloseWaitTime();

    void ToSendFrame(std::shared_ptr<IFrame> frame);
    void ActiveSendStream(std::shared_ptr<IStream> stream);
    void ActiveSend();

    void InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string reason);
    void ImmediateClose(uint64_t error, uint16_t tigger_frame, std::string reason);
    void InnerStreamClose(uint64_t stream_id);

    void AddConnectionId(ConnectionID& id);
    void RetireConnectionId(ConnectionID& id);
    
    std::shared_ptr<IStream> MakeStream(uint32_t init_size, uint64_t stream_id, StreamDirection sd);

    virtual void WriteCryptoData(std::shared_ptr<common::IBufferRead> buffer, int32_t err) = 0;

    // record bytes received on candidate path to increase amp budget while probing
    virtual void OnCandidatePathDatagramReceived(const common::Address& addr, uint32_t bytes) override {
        if (path_probe_inflight_ && (addr == candidate_peer_addr_)) {
            send_manager_.OnCandidatePathBytesReceived(bytes);
        }
    }
    virtual common::Address AcquireSendAddress() override {
        // For now, always send to active peer address. Candidate is used for validation step later.
        if (path_probe_inflight_) {
            return candidate_peer_addr_;
        }
        return peer_addr_;
    }

protected:
    // timer task
    common::TimerTask idle_timeout_task_;
    // transport param verify done
    TransportParam transport_param_;
    // connection memory pool
    std::shared_ptr<common::BlockMemoryPool> alloter_;
    // last time communicate, use to idle shutdown
    uint64_t last_communicate_time_; 
    // streams
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_map_;
    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;

    uint8_t pending_ecn_ {0};
    bool ecn_enabled_;
    // flow control
    FlowControl flow_control_;
    RecvControl recv_control_;
    SendManager send_manager_;
    // crypto
    ConnectionCrypto connection_crypto_;
    // token
    std::string token_;
    std::shared_ptr<TLSConnection> tls_connection_;

    // connection state
    enum class ConnectionStateType {
        kStateConnecting,
        kStateConnected,
        kStateDraining,
        kStateClosed,
    };
    ConnectionStateType state_;

    // hint for early-data scheduling: whether any application stream (id != 0) has pending send
    bool has_app_send_pending_ = false;
    // Track whether Initial packet has been sent in 0-RTT scenarios
    bool initial_packet_sent_ = false;

    // candidate path state (address only for now)
    common::Address candidate_peer_addr_;
    bool path_probe_inflight_ {false};
    uint8_t pending_path_challenge_data_[8] {0};
    common::TimerTask path_probe_task_;
    uint32_t probe_retry_count_ {0};
    uint32_t probe_retry_delay_ms_ {0};

    void StartPathValidationProbe();
    // Anti-amplification: while path is unvalidated, restrict sending
    void EnterAntiAmplification();
    void ExitAntiAmplification();
    void ScheduleProbeRetry();
};

}
}

#endif