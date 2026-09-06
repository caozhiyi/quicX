#ifndef QUIC_QUICX_MSG_RECEIVER
#define QUIC_QUICX_MSG_RECEIVER

#include <memory>
#include <mutex>
#include <unordered_map>

#include "quic/quicx/if_master.h"
#include "quic/quicx/if_worker.h"
#include "quic/udp/if_receiver.h"

namespace quicx {
namespace quic {

class Master:
    public IMaster,
    public IPacketReceiver,
    public IConnectionIDNotify,
    public std::enable_shared_from_this<Master> {
public:
public:
    Master(bool ecn_enabled, std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~Master();

    virtual void Init();

    // add a worker
    virtual void AddWorker(std::shared_ptr<IWorker> worker) override;
    // add listener
    virtual bool AddListener(common::SocketHandle listener_sock) override;
    virtual bool AddListener(const std::string& ip, uint16_t port) override;
    virtual bool RemoveListener(int32_t listener_sock) override;

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

    // Guards cid_worker_map_. It is read on the receiver thread (OnPacket) and
    // written on worker threads (AddConnectionID/RetireConnectionID during
    // handshake). Without this the unordered_map is accessed concurrently with
    // no synchronization, which under multi-worker / multi-client load causes
    // misrouted packets and random handshake failures.
    mutable std::mutex cid_map_mutex_;

    struct ListenerInfo {
        std::string ip;
        uint16_t port;
        common::SocketHandle sock;  // fd <= 0: use ip/port instead
    };
    std::vector<ListenerInfo> pending_listeners_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_QUICX_MSG_RECEIVER