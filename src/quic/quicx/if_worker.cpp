#include "quic/quicx/if_worker.h"
#include "quic/quicx/worker_client.h"
#include "quic/quicx/worker_server.h"

namespace quicx {
namespace quic {

std::shared_ptr<IWorker> IWorker::MakeClientWorker(const QuicConfig& config, 
    std::shared_ptr<TLSCtx> ctx, 
    const QuicTransportParams& params,
    connection_state_callback connection_handler) {
    return std::make_shared<ClientWorker>(config, ctx, params, connection_handler);
}

std::shared_ptr<IWorker> IWorker::MakeServerWorker(const QuicServerConfig& config, 
    std::shared_ptr<TLSCtx> ctx, 
    const QuicTransportParams& params,
    connection_state_callback connection_handler) {
    return std::make_shared<ServerWorker>(config, ctx, params, connection_handler);
}

}
}
