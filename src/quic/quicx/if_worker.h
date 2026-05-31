#ifndef QUIC_QUICX_IF_WORKER
#define QUIC_QUICX_IF_WORKER

#include <memory>
#include <string>

#include "quic/quicx/msg_parser.h"

namespace quicx {
namespace quic {

class IConnectionIDNotify {
public:
    virtual void AddConnectionID(ConnectionID& cid, const std::string& worker_id) = 0;
    virtual void RetireConnectionID(ConnectionID& cid, const std::string& worker_id) = 0;
};

// Worker interface
// Worker handles packets in local thread or other thread
class IWorker {
public:
    IWorker() {}
    virtual ~IWorker() {}
    // Get the worker id
    virtual std::string GetWorkerId() = 0;
    // Handle packets
    virtual void HandlePacket(PacketParseResult& packet_info) = 0;
    // Process pending internal tasks (e.g. sending queued data)
    virtual void Process() {}
    // add a connection id notify
    virtual void SetConnectionIDNotify(std::shared_ptr<IConnectionIDNotify> connection_id_notify) {
        connection_id_notify_ = connection_id_notify;
    }

    // Release all per-connection state held by this worker: conn_map_,
    // connecting_set_, active-send buffer, handshake timers, etc.
    //
    // Why this exists: every BaseConnection holds a
    // std::shared_ptr<IEventLoop>, and every Stream likewise. The event
    // loop in turn owns the worker (via AddFixedProcess in single-thread
    // mode). That means as long as any connection survives inside the
    // worker's bookkeeping maps, the whole object graph is kept alive by
    // mutual references and ~BaseConnection() never runs — observed as
    // ~120 KB per connection leaking across connect/disconnect cycles in
    // the profile_rss_lifecycle driver (P4).
    //
    // Threading contract (IMPORTANT): Owners (QuicClient / QuicServer
    // dtor) MUST call this AFTER Stop()+Join()-ing **the worker's own
    // event-loop thread** (in multi-thread mode that is the
    // WorkerWithThread; in single-thread mode the master thread is also
    // the worker thread, so Stop+Join on master is enough). Once the loop
    // thread has exited, no concurrent timer/IO callback can race with
    // Shutdown(), so the implementation can mutate worker internals
    // without synchronisation, and — critically — must NOT invoke
    // EventLoop::AddTimer/RemoveTimer/RegisterFd/... from this thread:
    // those are guarded by AssertInLoopThread() and would abort.
    virtual void Shutdown() {}

protected:
    std::weak_ptr<IConnectionIDNotify> connection_id_notify_;
};

}  // namespace quic
}  // namespace quicx

#endif