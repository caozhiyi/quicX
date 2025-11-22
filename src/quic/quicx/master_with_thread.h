#ifndef QUIC_QUICX_MSG_RECEIVER_WITH_THREAD
#define QUIC_QUICX_MSG_RECEIVER_WITH_THREAD

#include <thread>
#include "quic/quicx/master.h"
#include "common/thread/thread.h"
#include "common/structure/thread_safe_queue.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace quic {

class MasterWithThread: public Master, public common::Thread {
public:
    MasterWithThread(bool ecn_enabled);
    virtual ~MasterWithThread();

    void Run() override;

    void Stop() override;

    // add a new connection id
    virtual void AddConnectionID(ConnectionID& cid, const std::string& worker_id) override;
    // retire a connection id
    virtual void RetireConnectionID(ConnectionID& cid, const std::string& worker_id) override;
    // add listener (override to ensure socket is registered in Master thread's EventLoop)
    virtual bool AddListener(int32_t listener_sock) override;
    virtual bool AddListener(const std::string& ip, uint16_t port) override;
    // process the master
    virtual void Process() override;
    // post a task to the master's event loop
    virtual void PostTask(std::function<void()> task);
    // get the event loop
    virtual std::shared_ptr<common::IEventLoop> GetEventLoop();

private:
    void DoUpdateConnectionID();

private:
    std::shared_ptr<common::IEventLoop> event_loop_;  // Saved EventLoop for cross-thread access
    enum ConnectionOperation {
        ADD_CONNECTION_ID = 0,
        RETIRE_CONNECTION_ID = 1
    };
    struct ConnectionOpInfo {
        ConnectionOperation operation_;
        ConnectionID cid_;
        std::string worker_id_;
    };
    common::ThreadSafeQueue<ConnectionOpInfo> connection_op_queue_;
    common::ThreadSafeQueue<std::function<void()>> pending_tasks_;  // Tasks posted before EventLoop is initialized
};

}  // namespace quic
}  // namespace quicx

#endif