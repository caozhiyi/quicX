#ifndef QUIC_QUICX_IF_WORKER
#define QUIC_QUICX_IF_WORKER

#include <string>
#include <memory>

#include "quic/quicx/msg_parser.h"
#include "common/network/if_event_loop.h"

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
    // add a connection id notify
    virtual void SetConnectionIDNotify(std::shared_ptr<IConnectionIDNotify> connection_id_notify) {
        connection_id_notify_ = connection_id_notify;
    }

protected:
    std::weak_ptr<IConnectionIDNotify> connection_id_notify_;
};

}  // namespace quic
}  // namespace quicx

#endif