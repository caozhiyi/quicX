#ifndef QUIC_QUICX_NEW_SIMPLIFIED_CONNECTION_MANAGER_FACTORY
#define QUIC_QUICX_NEW_SIMPLIFIED_CONNECTION_MANAGER_FACTORY

#include <memory>
#include "quic/quicx_new/if_connection_manager.h"

namespace quicx {
namespace quic {

class SimplifiedConnectionManagerFactory {
public:
    // Create simplified connection manager with specified worker count
    static std::shared_ptr<IConnectionManager> Create(size_t worker_count = 4);
};

}
}

#endif 