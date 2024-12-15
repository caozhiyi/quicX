#ifndef QUIC_CONNECTION_BASE_CONNECTION
#define QUIC_CONNECTION_BASE_CONNECTION

#include <set>
#include <list>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include "quic/stream/if_send_stream.h"
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
        std::function<void(uint64_t/*cid hash*/, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t/*cid hash*/)> retire_conn_id_cb);
    virtual ~BaseConnection();

    virtual uint64_t GetConnectionIDHash();
    virtual std::shared_ptr<ISendStream> MakeSendStream();
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream();

    virtual void Close(uint64_t error);

    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer);

    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets);

    virtual EncryptionLevel GetCurEncryptionLevel() { return connection_crypto_.GetCurEncryptionLevel(); }

    virtual uint64_t GetSock() { return send_sock_; }
    
    virtual void SetPeerAddress(const common::Address&& addr) { peer_addr_ = std::move(addr); }
    virtual const common::Address GetPeerAddress() { return peer_addr_; }

    virtual void SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb);

    virtual uint64_t GetConnectionHashCode() { return 0; }

protected:
    virtual bool OnInitialPacket(std::shared_ptr<IPacket> packet);
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);
    virtual bool On1rttPacket(std::shared_ptr<IPacket> packet);

    virtual bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);
    virtual bool OnStreamFrame(std::shared_ptr<IFrame> frame);

    virtual bool OnAckFrame(std::shared_ptr<IFrame> frame, uint16_t crypto_level);
    virtual bool OnCryptoFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnNewTokenFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnMaxDataFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnDataBlockFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnStreamBlockFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnMaxStreamFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame);

    virtual void ActiveSendStream(std::shared_ptr<IStream> stream);

    virtual void InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string resion);
    virtual void InnerStreamClose(uint64_t stream_id);
    virtual void OnTransportParams(TransportParam& remote_tp);

    void AddConnectionId(uint64_t cid_hash);

    bool OnNormalPacket(std::shared_ptr<IPacket> packet);
    void ActiveSend();

private:
    enum StreamType {
        ST_BIDIRECTIONAL = 1,
        ST_RECV,
        ST_SEND,
    };
    std::shared_ptr<IStream> MakeStream(uint32_t init_size, uint64_t stream_id, StreamType st);

protected:
    uint64_t send_sock_;
    common::Address peer_addr_;

    // connection will to close
    bool to_close_;
    // last time communicate, use to idle shutdown
    uint64_t last_communicate_time_; 

    // transport param verify done
    TransportParam transport_param_;

    // streams
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_map_;

    // connection memory pool
    std::shared_ptr<common::BlockMemoryPool> alloter_;
    
    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;
    std::function<void(uint64_t/*cid hash*/, std::shared_ptr<IConnection>)> add_conn_id_cb_;

    // waitting send frames
    std::list<std::shared_ptr<IFrame>> frames_list_;

    // flow control
    std::shared_ptr<FlowControl> flow_control_;
    RecvControl recv_control_;
    SendManager send_manager_;

    // crypto
    ConnectionCrypto connection_crypto_;
   
    // token
    std::string token_;

    // active to send
    bool is_active_send_;
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb_;
};

}
}

#endif