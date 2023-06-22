#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/udp/udp_sender.h"
#include "quic/common/version.h"
#include "quic/packet/init_packet.h"
#include "quic/udp/udp_packet_out.h"
#include "quic/packet/packet_decode.h"
#include "quic/process/server_processor.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

ServerProcessor::ServerProcessor():
    _run(true) {
    _timer = MakeTimer1Min();
}

ServerProcessor::~ServerProcessor() {

}

bool ServerProcessor::HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet) {
    LOG_INFO("get packet from %s", udp_packet->GetPeerAddress().AsString().c_str());

    // dispatch packet
    auto packets = udp_packet->GetPackets();
    uint64_t cid_code = udp_packet->GetConnectionHashCode();
    auto conn = _conn_map.find(cid_code);
    if (conn != _conn_map.end()) {
        conn->second->OnPackets(packets);
        return true;
    }

    // check init packet?
    if (!InitPacketCheck(packets[0])) {
        return false;
    }

    uint8_t* cid;
    uint16_t len = 0;
    udp_packet->GetConnection(cid, len);

    auto new_conn = std::make_shared<ServerConnection>(_ctx);
    new_conn->AddConnectionId(cid, len);
    _conn_map[cid_code] = new_conn;
    new_conn->OnPackets(packets);

    return true;
}

bool ServerProcessor::HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets) {
    for (size_t i = 0; i < udp_packets.size(); i++) {
        HandlePacket(udp_packets[i]);
    }
    return true;
}

bool ServerProcessor::InitPacketCheck(std::shared_ptr<IPacket> packet) {
    if (packet->GetHeader()->GetPacketType() != PT_INITIAL) {
        LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    uint32_t version = ((LongHeader*)init_packet->GetHeader())->GetVersion();
    if (!VersionCheck(version)) {
        // TODO may generate a version negotiation packet
        return false;
    }

    return true;
}

void ServerProcessor::MainLoop() {
    if (!_recv_function) {
        LOG_ERROR("recv function is not set.");
        return;
    }

    _cur_time = UTCTimeMsec();
    while (_run) {
        if (_process_type & ServerProcessor::PT_RECV) {
            ProcessRecv();
        }
        
        if (_process_type & ServerProcessor::PT_SEND) {
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

void ServerProcessor::ActiveSendConnection(IConnection* conn) {
    _active_send_connection_list.push_back(conn);
    _process_type |= ServerProcessor::PT_SEND;
}

void ServerProcessor::WeakUp() {
    _notify.notify_one();
}

void ServerProcessor::ProcessRecv() {
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

void ServerProcessor::ProcessTimer() {
    uint64_t now = UTCTimeMsec();
    uint32_t run_time = now - _cur_time;
    _cur_time = now;

    _timer->TimerRun(run_time);
}

void ServerProcessor::ProcessSend() {
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

}