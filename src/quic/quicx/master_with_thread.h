#ifndef QUIC_QUICX_MSG_RECEIVER_WITH_THREAD
#define QUIC_QUICX_MSG_RECEIVER_WITH_THREAD

#include "quic/quicx/master.h"
#include "common/thread/thread.h"
#include "common/structure/thread_safe_queue.h"

namespace quicx {
namespace quic {

class MasterWithThread:
    public Master,
    public common::Thread {
public:
    MasterWithThread(bool ecn_enabled);
    virtual ~MasterWithThread();
    
    void Run() override;

    void Stop() override;

    // add a new connection id
    virtual void AddConnectionID(ConnectionID& cid, const std::string& worker_id) override;
    // retire a connection id
    virtual void RetireConnectionID(ConnectionID& cid, const std::string& worker_id) override;
    // process the master
    virtual void Process() override;
    // get the event loop
    std::shared_ptr<common::IEventLoop> GetEventLoop();

private:
    void DoUpdateConnectionID();

private:
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
};

}
}

#endif