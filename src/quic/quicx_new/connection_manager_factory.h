#ifndef QUIC_QUICX_NEW_CONNECTION_MANAGER_FACTORY
#define QUIC_QUICX_NEW_CONNECTION_MANAGER_FACTORY

#include <memory>
#include "quic/quicx_new/if_connection_manager.h"

namespace quicx {
namespace quic {

enum class ConnectionManagerType {
    SINGLE_THREAD,    // Single thread mode
    MULTI_THREAD      // Multi thread mode
};

class ConnectionManagerFactory {
public:
    // Create connection manager based on type
    static std::shared_ptr<IConnectionManager> Create(ConnectionManagerType type, size_t worker_count = 4);
    
    // Create single thread connection manager
    static std::shared_ptr<IConnectionManager> CreateSingleThread();
    
    // Create multi thread connection manager
    static std::shared_ptr<IConnectionManager> CreateMultiThread(size_t worker_count = 4);
};

}
}

#endif 