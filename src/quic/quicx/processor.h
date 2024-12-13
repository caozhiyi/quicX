#ifndef QUIC_QUICX_SERVER_PROCESSOR
#define QUIC_QUICX_SERVER_PROCESSOR

#include <memory>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "quic/udp/if_sender.h"
#include "common/timer/timer.h"
#include "quic/udp/if_receiver.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/quicx/if_processor.h"
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

/*
 message dispatcher processor, handle packet and timer in one thread
*/
class Processor:
    public IProcessor {
public:
    Processor(std::shared_ptr<ISender> sender,
        std::shared_ptr<IReceiver> receiver, std::shared_ptr<TLSCtx> ctx);
    virtual ~Processor();

    void Process();

    virtual std::shared_ptr<IConnection> MakeClientConnection();

protected:
    void ProcessRecv(uint32_t timeout_ms);
    void ProcessTimer();
    void ProcessSend();

    bool HandlePacket(std::shared_ptr<INetPacket> packet);
    void ActiveSendConnection(std::shared_ptr<IConnection> conn);
    void AddConnectionId(uint64_t cid_hash, std::shared_ptr<IConnection> conn);
    void RetireConnectionId(uint64_t cid_hash);

    bool InitPacketCheck(std::shared_ptr<IPacket> packet);

    static bool DecodeNetPakcet(std::shared_ptr<INetPacket> net_packet,
        std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);

protected:
    bool _do_send;

    std::shared_ptr<TLSCtx> _ctx;
    std::shared_ptr<ISender> _sender;
    std::shared_ptr<IReceiver> _receiver;

    std::function<void(uint64_t)> _add_connection_id_cb;
    std::function<void(uint64_t)> _retire_connection_id_cb;

    std::shared_ptr<common::BlockMemoryPool> _alloter;

    std::list<std::shared_ptr<IConnection>> _active_send_connection_list;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;

    thread_local static std::shared_ptr<common::ITimer> __timer;
};

}
}

#endif