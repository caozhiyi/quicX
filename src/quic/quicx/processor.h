#ifndef QUIC_QUICX_SERVER_PROCESSOR
#define QUIC_QUICX_SERVER_PROCESSOR

#include <memory>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "common/timer/timer.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/quicx/if_processor.h"
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

class Processor:
    public IProcessor {
public:
    Processor();
    virtual ~Processor();

    void Run();

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet);
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets);

    virtual void ActiveSendConnection(std::shared_ptr<IConnection> conn);

    virtual void WeakUp();

    virtual std::shared_ptr<IConnection> MakeClientConnection();

protected:
    virtual void ProcessRecv();
    virtual void ProcessTimer();
    virtual void ProcessSend();

    bool InitPacketCheck(std::shared_ptr<IPacket> packet);

    static bool GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);

protected:
    enum ProcessType {
        PT_RECV = 0x01,
        PT_SEND = 0x02,
    };
    uint32_t _process_type;

    std::shared_ptr<TLSCtx> _ctx;

    thread_local static std::shared_ptr<common::ITimer> __timer;

    std::mutex _notify_mutex;
    std::condition_variable _notify;

    uint32_t _max_recv_times;
    RecvFunction _recv_function;
    std::list<std::shared_ptr<IConnection>> _active_send_connection_list;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;
};

}
}

#endif