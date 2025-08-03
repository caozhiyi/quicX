#include "common/log/log.h"
#include "quic/quicx_new/connection_manager_factory.h"
#include "quic/quicx_new/single_thread_connection_manager.h"
#include "quic/quicx_new/multi_thread_connection_manager.h"


namespace quicx {
namespace quic {

std::shared_ptr<IConnectionManager> ConnectionManagerFactory::Create(ConnectionManagerType type, size_t worker_count) {
    switch (type) {
        case ConnectionManagerType::SINGLE_THREAD:
            return CreateSingleThread();
            
        case ConnectionManagerType::MULTI_THREAD:
            return CreateMultiThread(worker_count);
            
        default:
            common::LOG_ERROR("Unknown connection manager type: %d", static_cast<int>(type));
            return nullptr;
    }
}

std::shared_ptr<IConnectionManager> ConnectionManagerFactory::CreateSingleThread() {
    common::LOG_INFO("Creating single thread connection manager");
    return std::make_shared<SingleThreadConnectionManager>();
}

std::shared_ptr<IConnectionManager> ConnectionManagerFactory::CreateMultiThread(size_t worker_count) {
    common::LOG_INFO("Creating multi thread connection manager with %zu workers", worker_count);
    return std::make_shared<MultiThreadConnectionManager>(worker_count);
}

}
} 