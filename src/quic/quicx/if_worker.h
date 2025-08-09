#ifndef QUIC_QUICX_IF_WORKER
#define QUIC_QUICX_IF_WORKER

#include <memory>
#include <thread>

#include "quic/include/type.h"
#include "quic/quicx/msg_parser.h"
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

class IConnectionIDNotify {
public:
    virtual void AddConnectionID(ConnectionID& cid) = 0;
    virtual void RetireConnectionID(ConnectionID& cid) = 0;
};

// TODO how to implement a worker without new thread?

// Worker interface
// Worker handles packets in local thread or other thread
class IWorker {
public:
    IWorker() {}
    virtual ~IWorker() {}

    // Initialize the worker
    virtual void Init(std::shared_ptr<IConnectionIDNotify> connection_id_notify) = 0;
    // Destroy the worker
    virtual void Destroy() = 0;
    
    // Weakup the worker
    virtual void Weakup() = 0;

    // Join the worker
    virtual void Join() = 0;

    // Get the current thread id
    virtual std::thread::id GetCurrentThreadId() = 0;

    // Handle packets
    virtual void HandlePacket(PacketInfo& packet_info) = 0;

    enum WorkerType {
        kClientWorker,
        kServerWorker
    };
    // Make a worker
    static std::shared_ptr<IWorker> MakeWorker(WorkerType type, 
        std::shared_ptr<TLSCtx> ctx, 
        const QuicTransportParams& params,
        connection_state_callback connection_handler);
};

}
}

#endif