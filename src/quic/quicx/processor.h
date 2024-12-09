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

class Processor:
    public IProcessor {
public:
    Processor(std::shared_ptr<ISender> sender, std::shared_ptr<IReceiver> receiver, std::shared_ptr<TLSCtx> ctx);
    virtual ~Processor();

    void Run();

    virtual bool HandlePacket(std::shared_ptr<INetPacket> packet);

    virtual void ActiveSendConnection(std::shared_ptr<IConnection> conn);

    virtual void WeakUp();

    virtual std::shared_ptr<IConnection> MakeClientConnection();

protected:
    virtual void ProcessRecv();
    virtual void ProcessTimer();
    virtual void ProcessSend();

    bool InitPacketCheck(std::shared_ptr<IPacket> packet);

    static bool DecodeNetPakcet(std::shared_ptr<INetPacket> net_packet,
        std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);

protected:
    std::shared_ptr<TLSCtx> _ctx;
    std::shared_ptr<ISender> _sender;
    std::shared_ptr<IReceiver> _receiver;

    thread_local static std::shared_ptr<common::ITimer> __timer;

    std::mutex _notify_mutex;
    std::condition_variable _notify;

    std::function<void(uint64_t)> _add_connection_id_cb;
    std::function<void(uint64_t)> _retire_connection_id_cb;

    std::shared_ptr<common::BlockMemoryPool> _alloter;
    
    uint32_t _max_recv_times;
    std::list<std::shared_ptr<IConnection>> _active_send_connection_list;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;

private:
    bool _do_send;
};

}
}

#endif