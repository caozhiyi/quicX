#ifndef QUIC_PROCESS_SERVER_PROCESSOR
#define QUIC_PROCESS_SERVER_PROCESSOR

#include <list>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "common/timer/timer.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/process/processor_interface.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class ServerProcessor:
    public IProcessor {
public:
    ServerProcessor();
    virtual ~ServerProcessor();

    virtual bool HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet);
    virtual bool HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets);

    bool InitPacketCheck(std::shared_ptr<IPacket> packet);

    void MainLoop();

    void Quit();

    void ActiveSendConnection(IConnection* conn);

    void WeakUp();

    typedef std::function<std::shared_ptr<UdpPacketIn>/*recv packet*/()> RecvFunction;
    void SetRecvFunction(RecvFunction rf) { _recv_function = rf; }

protected:
    virtual void ProcessRecv();
    virtual void ProcessTimer();
    virtual void ProcessSend();

private:
    enum ProcessType {
        PT_RECV = 0x01,
        PT_SEND = 0x02,
    };

    bool _run;
    uint32_t _process_type;

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