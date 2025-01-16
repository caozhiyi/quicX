#ifndef QUIC_CONNECTION_IF_CONNECTION
#define QUIC_CONNECTION_IF_CONNECTION

#include <memory>
#include <functional>
#include "common/timer/timer.h"
#include "quic/crypto/tls/type.h"
#include "quic/connection/type.h"
#include "quic/packet/if_packet.h"
#include "common/network/address.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/if_stream_frame.h"
#include "quic/crypto/tls/tls_conneciton.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/include/if_quic_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {

class IConnection:
    public IQuicConnection {
public:
    IConnection(std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t cid_hash)> retire_conn_id_cb);
    virtual ~IConnection();

    //*************** outside interface ***************//
    virtual void SetUserData(void* user_data) { user_data_ = user_data; }
    virtual void* GetUserData() { return user_data_; }

    virtual void GetRemoteAddr(std::string& addr, uint32_t& port);

    // close the connection gracefully. that means all the streams will be closed gracefully.
    virtual void Close() = 0;

    // close the connection immediately. that means all the streams will be closed immediately.
    virtual void Reset(uint32_t error_code) = 0;

    // create a new stream, only supported send stream and bidirection stream.
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) = 0;
    // set the callback function to handle the stream state change.
    virtual void SetStreamStateCallBack(stream_state_callback cb) { stream_state_cb_ = cb; }

    //*************** inner interface ***************//
    virtual void AddTransportParam(TransportParamConfig& tp_config) = 0;
    virtual uint64_t GetConnectionIDHash() = 0;
    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_done) = 0;
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) = 0;
    virtual EncryptionLevel GetCurEncryptionLevel() = 0;

    // peer address
    virtual void SetPeerAddress(const common::Address& addr);
    virtual void SetPeerAddress(const common::Address&& addr);
    virtual const common::Address& GetPeerAddress();

    // if connection is transferred from other thread, below callbacks need to current thread
    virtual void SetTimer(std::shared_ptr<common::ITimer> timer) { /* TODO: implement */ }
    virtual void SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb);
    virtual void SetHandshakeDoneCB(std::function<void(std::shared_ptr<IConnection>)> cb);
    virtual void SetAddConnectionIdCB(std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> cb);
    virtual void SetRetireConnectionIdCB(std::function<void(uint64_t cid_hash)> cb);
    virtual void SetConnectionCloseCB(std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> cb);

protected:
    void* user_data_;
    common::Address peer_addr_;
    // callback
    std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb_;
    std::function<void(uint64_t cid_hash)> retire_conn_id_cb_;
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb_;
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb_;
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb_;

    stream_state_callback stream_state_cb_;

};

}
}

#endif