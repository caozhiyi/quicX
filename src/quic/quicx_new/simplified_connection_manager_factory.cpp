#include "quic/quicx_new/simplified_connection_manager_factory.h"
#include "quic/quicx_new/simplified_connection_manager.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

std::shared_ptr<IConnectionManager> SimplifiedConnectionManagerFactory::Create(size_t worker_count) {
    common::LOG_INFO("Creating simplified connection manager with %zu workers", worker_count);
    return std::make_shared<SimplifiedConnectionManager>(worker_count);
}

}
} 