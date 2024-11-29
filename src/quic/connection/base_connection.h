#ifndef QUIC_CONNECTION_BASE_CONNECTION
#define QUIC_CONNECTION_BASE_CONNECTION

#include <set>
#include <list>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include "quic/stream/if_send_stream.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_interface.h"
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
        ConnectionIDCB add_conn_id_cb,
        ConnectionIDCB retire_conn_id_cb);
    virtual ~BaseConnection();

    virtual std::shared_ptr<ISendStream> MakeSendStream();
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream();

    virtual void Close(uint64_t error);

    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer);

    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets);

    virtual EncryptionLevel GetCurEncryptionLevel() { return _connection_crypto.GetCurEncryptionLevel(); }

    virtual uint64_t GetSock() { return _send_sock; }
    
    virtual void SetPeerAddress(const common::Address&& addr) { _peer_addr = std::move(addr); }
    virtual common::Address* GetPeerAddress() { return &_peer_addr; }

    virtual void SetActiveConnectionCB(ActiveConnectionCB cb);

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
    uint64_t _send_sock;
    common::Address _peer_addr;

    // connection will to close
    bool _to_close;
    // last time communicate, use to idle shutdown
    uint64_t _last_communicate_time; 

    // transport param verify done
    TransportParam _transport_param;

    // streams
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> _streams_map;

    // connection memory pool
    std::shared_ptr<common::BlockMemoryPool> _alloter;
    
    // connection id
    std::shared_ptr<ConnectionIDManager> _local_conn_id_manager;
    std::shared_ptr<ConnectionIDManager> _remote_conn_id_manager;

    // wait send frames
    std::list<std::shared_ptr<IFrame>> _frames_list;

    // flow control
    std::shared_ptr<FlowControl> _flow_control;
    RecvControl _recv_control;
    SendManager _send_manager;

    // crypto
    ConnectionCrypto _connection_crypto;
   
    // token
    std::string _token;

    bool _is_active_send;
    ActiveConnectionCB _active_connection_cb;
};

}
}

#endif