#include "quic/quicx/if_worker.h"
#include "quic/quicx/worker_client.h"
#include "quic/quicx/worker_server.h"


namespace quicx {
namespace quic {

std::shared_ptr<IWorker> IWorker::MakeWorker(WorkerType type, 
    bool ecn_enabled,
    std::shared_ptr<TLSCtx> ctx, 
    const QuicTransportParams& params,
    connection_state_callback connection_handler) {
    if (type == kClientWorker) {
        return std::make_shared<ClientWorker>(ctx, ecn_enabled, params, connection_handler);
    } else if (type == kServerWorker) {
        return std::make_shared<ServerWorker>(ctx, ecn_enabled, params, connection_handler);
    }
    return nullptr;
}

}
}
