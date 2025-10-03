#ifndef QUIC_QUICX_WORKER_WITH_THREAD
#define QUIC_QUICX_WORKER_WITH_THREAD

#include "quic/quicx/if_worker.h"
#include "common/thread/thread.h"
#include "common/structure/thread_safe_block_queue.h"

namespace quicx {
namespace quic {

class WorkerWithThread:
    public common::Thread,
    public IWorker {
public:
    WorkerWithThread(std::unique_ptr<IWorker> worker_ptr);
    virtual ~WorkerWithThread();

    void Run() override;

    void Stop() override;
    // Get the worker id
    virtual std::string GetWorkerId() override;
    // Handle packets
    virtual void HandlePacket(PacketInfo& packet_info) override;
    // Get the event loop
    virtual std::shared_ptr<common::IEventLoop> GetEventLoop() override;

private:
    void ProcessRecv();

protected:
    std::string worker_id_;
    std::unique_ptr<IWorker> worker_ptr_;
    common::ThreadSafeBlockQueue<PacketInfo> packet_queue_;
};

}
}

#endif