#ifndef QUIC_QUICX_SERVER_PROCESSOR
#define QUIC_QUICX_SERVER_PROCESSOR

#include <memory>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "common/timer/timer.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/quicx/processor_interface.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class Processor:
    public IProcessor {
public:
    Processor();
    virtual ~Processor();

    void Run();

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet);
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets);

    virtual void ActiveSendConnection(IConnection* conn);

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

    thread_local static std::shared_ptr<ITimer> __timer;

    std::mutex _notify_mutex;
    std::condition_variable _notify;

    ConnectionIDCB _add_connection_id_cb;
    ConnectionIDCB _retire_connection_id_cb;

    uint32_t _max_recv_times;
    RecvFunction _recv_function;
    std::list<IConnection*> _active_send_connection_list;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;
};

}

#endif