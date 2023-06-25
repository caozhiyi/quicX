#ifndef QUIC_CONNECTION_BASE_CONNECTION
#define QUIC_CONNECTION_BASE_CONNECTION

#include <set>
#include <list>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include "quic/connection/transport_param.h"
#include "quic/stream/send_stream_interface.h"
#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_interface.h"
#include "quic/connection/controler/flow_control.h"

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

    virtual void OnPackets(std::vector<std::shared_ptr<IPacket>>& packets);

    virtual EncryptionLevel GetCurEncryptionLevel() { return _connection_crypto.GetCurEncryptionLevel(); }

    virtual uint64_t GetSock() { return _send_sock; }
    
    virtual void SetPeerAddress(const Address&& addr) { _peer_addr = std::move(addr); }
    virtual Address* GetPeerAddress() { return &_peer_addr; }

    virtual uint64_t GetConnectionHashCode() { return 0; }

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
    virtual void OnTransportParams(TransportParam& remote_tp);

    bool OnNormalPacket(std::shared_ptr<IPacket> packet);

private:
    enum StreamType {
        ST_BIDIRECTIONAL = 1,
        ST_RECV,
        ST_SEND,
    };
    std::shared_ptr<IStream> MakeStream(uint32_t init_size, uint64_t stream_id, StreamType st);

protected:
    uint64_t _send_sock;
    Address _peer_addr;

    // connection will to close
    bool _to_close;
    // last time communicate, use to idle shutdown
    uint64_t _last_communicate_time; 

    // transport param verify done
    TransportParam _transport_param;

    // streams
    std::list<IStream*> _active_send_stream_list;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> _streams_map;

    // connection memory pool
    std::shared_ptr<BlockMemoryPool> _alloter;
    
    // connection id
    std::unordered_set<std::string> _conn_id_set;
    std::list<std::shared_ptr<IFrame>> _frames_list;

    // flow control
    FlowControl _flow_control;

    // crypto
    ConnectionCrypto _connection_crypto;
   
    // token
    std::string _token;
};

}

#endif