#ifndef QUIC_PROCESS_PROCESSOR_INTERFACE
#define QUIC_PROCESS_PROCESSOR_INTERFACE

#include <memory>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "common/timer/timer.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class IProcessor {
public:
    IProcessor();
    virtual ~IProcessor() {}

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet) = 0;
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets) = 0;

    virtual void MainLoop();

    virtual void Quit();

    virtual void ActiveSendConnection(IConnection* conn);

    virtual void WeakUp();

    typedef std::function<std::shared_ptr<UdpPacketIn>/*recv packet*/()> RecvFunction;
    void SetRecvFunction(RecvFunction rf) { _recv_function = rf; }

    std::shared_ptr<TLSCtx> GetCtx() { return _ctx; }

protected:
    virtual void ProcessRecv();
    virtual void ProcessTimer();
    virtual void ProcessSend();

    static bool GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);
protected:
    enum ProcessType {
        PT_RECV = 0x01,
        PT_SEND = 0x02,
    };

    bool _run;
    uint32_t _process_type;

    std::shared_ptr<TLSCtx> _ctx;

    uint64_t _cur_time;
    std::shared_ptr<Timer> _timer;

    std::mutex _notify_mutex;
    std::condition_variable _notify;

    uint32_t _max_recv_times;
    RecvFunction _recv_function;
    std::list<IConnection*> _active_send_connection_list;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;
};

}

#endif