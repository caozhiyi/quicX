#ifndef QUIC_QUICX_WORKER_WITH_THREAD
#define QUIC_QUICX_WORKER_WITH_THREAD

#include "quic/quicx/if_worker.h"
#include "common/thread/thread.h"
#include <quicx/common/if_event_loop.h>
#include "common/structure/thread_safe_block_queue.h"

namespace quicx {
namespace quic {

class WorkerWithThread: public common::Thread, public IWorker {
public:
    WorkerWithThread(std::shared_ptr<common::IEventLoop> event_loop, std::shared_ptr<IWorker> worker_ptr);
    virtual ~WorkerWithThread();

    void Run() override;

    void Stop() override;
    // Get the worker id
    virtual std::string GetWorkerId() override;
    // Handle packets
    virtual void HandlePacket(PacketParseResult& packet_info) override;
    // post a task to the worker's event loop
    virtual void PostTask(std::function<void()> task);
    // get the worker
    std::shared_ptr<IWorker> GetWorker() { return worker_ptr_; }
    // get the worker's event loop
    std::shared_ptr<common::IEventLoop> GetEventLoop() { return event_loop_.lock(); }

private:
    void ProcessRecv();

protected:
    std::string worker_id_;
    std::shared_ptr<IWorker> worker_ptr_;
    std::weak_ptr<common::IEventLoop> event_loop_;  // Observer reference (owner is QuicClient/QuicServer)
    common::ThreadSafeBlockQueue<PacketParseResult> packet_queue_;
};

}  // namespace quic
}  // namespace quicx

#endif