#ifndef QUIC_QUICX_MSG_RECEIVER
#define QUIC_QUICX_MSG_RECEIVER

#include <memory>
#include <unordered_map>

#include "quic/udp/if_receiver.h"
#include "quic/quicx/if_master.h"
#include "quic/quicx/if_worker.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace quic {

class Master:
    public IMaster,
    public IPacketReceiver,
    public IConnectionIDNotify,
    public std::enable_shared_from_this<Master> {
public:
public:
    Master(bool ecn_enabled);
    virtual ~Master();

    virtual void Init();

    // add a worker
    virtual void AddWorker(std::shared_ptr<IWorker> worker) override;
    // add listener
    virtual bool AddListener(int32_t listener_sock) override;
    virtual bool AddListener(const std::string& ip, uint16_t port) override;

    // add a new connection id
    virtual void AddConnectionID(ConnectionID& cid, const std::string& worker_id) override;
    // retire a connection id
    virtual void RetireConnectionID(ConnectionID& cid, const std::string& worker_id) override;
    // process the master
    virtual void Process() override {};

private:
    void OnPacket(std::shared_ptr<NetPacket>& pkt) override;

protected:
    bool ecn_enabled_;
    std::shared_ptr<IReceiver> receiver_;
    std::unordered_map<uint64_t, std::string> cid_worker_map_;
    std::unordered_map<std::string, std::shared_ptr<IWorker>> worker_map_;

    struct ListenerInfo {
        std::string ip;
        uint16_t port;
        int32_t sock = -1;
    };
    std::vector<ListenerInfo> pending_listeners_;
};

}  // namespace quic
}  // namespace quicx

#endif