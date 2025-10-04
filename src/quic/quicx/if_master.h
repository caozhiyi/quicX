#ifndef QUIC_QUICX_IF_MASTER
#define QUIC_QUICX_IF_MASTER

#include <memory>

#include "quic/quicx/if_worker.h"
#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

// Worker manager interface
class IMaster {
public:
    IMaster() {}
    virtual ~IMaster() {}

    virtual void AddWorker(std::shared_ptr<IWorker> worker) = 0;

    // add listener
    virtual bool AddListener(int32_t listener_sock) = 0;
    virtual bool AddListener(const std::string& ip, uint16_t port) = 0;

    // add a new connection id
    virtual void AddConnectionID(ConnectionID& cid, const std::string& worker_id) = 0;
    // retire a connection id
    virtual void RetireConnectionID(ConnectionID& cid, const std::string& worker_id) = 0;
    // process the master
    virtual void Process() = 0;
};

}
}

#endif