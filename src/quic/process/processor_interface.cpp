#include <chrono>
#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/udp/udp_sender.h"
#include "quic/udp/udp_packet_out.h"
#include "quic/packet/header/long_header.h"
#include "quic/process/processor_interface.h"

namespace quicx {

IProcessor::IProcessor():
    _run(false) {
    _ctx = std::make_shared<TLSCtx>();
    if (!_ctx->Init()) {
        LOG_ERROR("tls ctx init faliled.");
    }
    _timer = MakeTimer1Min();
}

void IProcessor::MainLoop() {
    if (!_recv_function) {
        LOG_ERROR("recv function is not set.");
        return;
    }

    _cur_time = UTCTimeMsec();
    while (_run) {
        if (_process_type & IProcessor::PT_RECV) {
            ProcessRecv();
        }
        
        if (_process_type & IProcessor::PT_SEND) {
            ProcessSend();
        }
        _process_type = 0;

        ProcessTimer();

        int64_t wait_time = _timer->MinTime();
        wait_time = wait_time < 0 ? 1000 : wait_time;

        std::unique_lock<std::mutex> lock(_notify_mutex);
        _notify.wait_for(lock, std::chrono::milliseconds(wait_time));
    }
}

void IProcessor::Quit() {
    _run = false;
}

void IProcessor::ActiveSendConnection(IConnection* conn) {
    _active_send_connection_list.push_back(conn);
    _process_type |= IProcessor::PT_SEND;
}

void IProcessor::WeakUp() {
    _notify.notify_one();
}

void IProcessor::ProcessRecv() {
    int times = _max_recv_times;
    while (times > 0) {
        auto packet = _recv_function();
        if (!packet) {
            break;
        }
        HandlePacket(packet);
        times--;
    }
}

void IProcessor::ProcessTimer() {
    uint64_t now = UTCTimeMsec();
    uint32_t run_time = now - _cur_time;
    _cur_time = now;

    _timer->TimerRun(run_time);
}

void IProcessor::ProcessSend() {
    static thread_local uint8_t buf[1500] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, sizeof(buf));

    std::shared_ptr<UdpPacketOut> packet_out;
    for (auto iter = _active_send_connection_list.begin(); iter != _active_send_connection_list.end(); ++iter) {
        if (!(*iter)->GenerateSendData(buffer)) {
            LOG_ERROR("generate send data failed.");
            continue;
        }

        packet_out->SetData(buffer);
        packet_out->SetOutsocket((*iter)->GetSock());
        packet_out->SetPeerAddress((*iter)->GetPeerAddress());

        if (!UdpSender::DoSend(packet_out)) {
            LOG_ERROR("udp send failed.");
        }
    }
    _active_send_connection_list.clear();
}

bool IProcessor::GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len) {
    if (packets.empty()) {
        LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    auto first_packet_header = packets[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PHT_SHORT_HEADER) {
        // todo get short header dcid
    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        len = long_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)long_header->GetDestinationConnectionId();
    }
    return true;
}

}