#ifndef QUIC_CONNECTION_IF_CONNECTION
#define QUIC_CONNECTION_IF_CONNECTION

#include <functional>
#include <memory>

#include "common/network/address.h"

#include "quic/connection/connection_id.h"
#include "quic/connection/type.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/tls/type.h"
#include "quic/include/if_quic_connection.h"
#include "quic/packet/if_packet.h"
#include "quic/udp/if_sender.h"

namespace quicx {
namespace quic {

class IConnection: public IQuicConnection, public std::enable_shared_from_this<IConnection> {
public:
    IConnection(std::shared_ptr<ISender> sender, std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t, const std::string&)> connection_close_cb);
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
    // add a timer, implementation in BaseConnection
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms) = 0;
    // remove a timer, implementation in BaseConnection
    virtual void RemoveTimer(uint64_t timer_id) = 0;

    //*************** inner interface ***************//
    virtual void AddTransportParam(const QuicTransportParams& tp_config) = 0;
    virtual uint64_t GetConnectionIDHash() = 0;
    // Get all local CID hashes for this connection (for cleanup on close)
    virtual std::vector<uint64_t> GetAllLocalCIDHashes() = 0;
    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_done) = 0;
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) = 0;
    // provide ECN value for the next OnPackets call (per received datagram)
    virtual void SetPendingEcn(uint8_t ecn) = 0;
    virtual EncryptionLevel GetCurEncryptionLevel() = 0;
    // test-only hook: provide cryptographer for decoding packets in unit tests
    virtual std::shared_ptr<ICryptographer> GetCryptographerForTest(uint16_t /*level*/) { return nullptr; }

    // connection transfer between threads
    virtual void ThreadTransferBefore() = 0;
    virtual void ThreadTransferAfter() = 0;

    // peer address
    virtual void SetPeerAddress(const common::Address& addr);
    virtual void SetPeerAddress(const common::Address&& addr);
    virtual const common::Address& GetPeerAddress();
    // observe peer address from incoming datagrams; default no-op
    virtual void OnObservedPeerAddress(const common::Address& addr) { (void)addr; }
    // notify candidate-path datagram was received with its address and size; default no-op
    virtual void OnCandidatePathDatagramReceived(const common::Address& addr, uint32_t bytes) {
        (void)addr;
        (void)bytes;
    }
    // get destination address for next datagram; default current peer address
    virtual common::Address AcquireSendAddress() { return peer_addr_; }

    void SetSocket(int32_t sockfd) { sockfd_ = sockfd; }
    int32_t GetSocket() const { return sockfd_; }

    // send a packet immediately
    void SendImmediate(std::shared_ptr<common::IBuffer> buffer);
    // send a packet deferred
    void SendDeferred();
    // process sending
    // return true if sent done, false if need to send again later
    bool DoSend();

    virtual void CloseInternal() = 0;

protected:
    void* user_data_;
    int32_t sockfd_;
    common::Address peer_addr_;
    // callback
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb_;
    std::function<void(ConnectionID&)> retire_conn_id_cb_;
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb_;
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb_;
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb_;

    stream_state_callback stream_state_cb_;
    // udp packet sender
    std::shared_ptr<ISender> sender_;
};

}  // namespace quic
}  // namespace quicx

#endif