#ifndef QUIC_QUICX_MSG_RECEIVER
#define QUIC_QUICX_MSG_RECEIVER

#include <memory>
#include <thread>
#include <unordered_map>

#include "common/timer/if_timer.h"
#include "quic/udp/if_receiver.h"
#include "common/thread/thread.h"
#include "quic/quicx/if_master.h"
#include "quic/quicx/if_worker.h"
#include "common/structure/thread_safe_queue.h"
#include "quic/connection/connection_id_manager.h"

namespace quicx {
namespace quic {

class Master:
    public IMaster,
    public common::Thread,
    public IConnectionIDNotify,
    public std::enable_shared_from_this<Master> {
public:
    Master();
    virtual ~Master();

    // Initialize as client
    virtual bool InitAsClient(int32_t thread_num, const QuicTransportParams& params, connection_state_callback connection_state_cb) override;
    // Initialize as server
    virtual bool InitAsServer(int32_t thread_num, const std::string& cert_file, const std::string& key_file, const std::string& alpn, 
        const QuicTransportParams& params, connection_state_callback connection_state_cb) override;
    virtual bool InitAsServer(int32_t thread_num, const char* cert_pem, const char* key_pem, const std::string& alpn, 
        const QuicTransportParams& params, connection_state_callback connection_state_cb) override;
    // Destroy
    virtual void Destroy() override;

    // Weakup
    virtual void Weakup() override;

    // Join
    virtual void Join() override;

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) override;

    // connect to a quic server
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms) override;
    // connect to a quic server with a specific resumption session (DER bytes) for this connection
    // passing non-empty session enables 0-RTT on resumption if ticket allows
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) override;
    
    // add listener
    virtual void AddListener(uint64_t listener_sock) override;
    virtual void AddListener(const std::string& ip, uint64_t port) override;

    // add a new connection id
    virtual void AddConnectionID(ConnectionID& cid) override;
    // retire a connection id
    virtual void RetireConnectionID(ConnectionID& cid) override;

private:
    void Run() override;

    void DoRecv();
    void DoUpdateConnectionID();

private:
    enum ConnectionOperation {
        ADD_CONNECTION_ID = 0,
        RETIRE_CONNECTION_ID = 1
    };
    struct ConnectionOpInfo {
        ConnectionOperation operation_;
        ConnectionID cid_;
        std::thread::id worker_id_;
    };
    common::ThreadSafeQueue<ConnectionOpInfo> connection_op_queue_;

private:
    std::shared_ptr<common::ITimer> timer_;
    std::shared_ptr<IReceiver> receiver_;
    std::unordered_map<uint64_t, std::thread::id> cid_worker_map_;
    std::unordered_map<std::thread::id, std::shared_ptr<IWorker>> worker_map_;
};

}
}

#endif