#ifndef QUIC_QUICX_MSG_RECEIVER_WITH_THREAD
#define QUIC_QUICX_MSG_RECEIVER_WITH_THREAD

#include <future>

#include <quicx/common/if_event_loop.h>
#include "common/structure/thread_safe_queue.h"
#include "common/thread/thread.h"
#include "quic/quicx/master.h"

namespace quicx {
namespace quic {

class MasterWithThread: public Master, public common::Thread {
public:
    MasterWithThread(bool ecn_enabled, std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~MasterWithThread();

    void Run() override;

    void Stop() override;

    // Block until the event loop thread has finished Init().
    // Returns true if Init() succeeded, false otherwise.
    bool WaitUntilReady();

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

private:
    void DoUpdateConnectionID();

private:
    std::weak_ptr<common::IEventLoop> event_loop_;  // Observer reference (owner is QuicClient/QuicServer)
    enum ConnectionOperation { ADD_CONNECTION_ID = 0, RETIRE_CONNECTION_ID = 1 };
    struct ConnectionOpInfo {
        ConnectionOperation operation_;
        ConnectionID cid_;
        std::string worker_id_;
    };
    common::ThreadSafeQueue<ConnectionOpInfo> connection_op_queue_;
    common::ThreadSafeQueue<std::function<void()>> pending_tasks_;  // Tasks posted before EventLoop is initialized

    std::promise<bool> ready_promise_;
    std::shared_future<bool> ready_future_;
};

}  // namespace quic
}  // namespace quicx

#endif