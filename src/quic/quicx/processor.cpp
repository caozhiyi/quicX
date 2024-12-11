#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/udp/udp_sender.h"
#include "quic/udp/udp_sender.h"
#include "quic/quicx/processor.h"
#include "quic/udp/udp_receiver.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

uint32_t __max_recv_times = 32; // todo add to config
thread_local std::shared_ptr<common::ITimer> Processor::__timer = common::MakeTimer();

Processor::Processor(std::shared_ptr<ISender> sender, std::shared_ptr<IReceiver> receiver, std::shared_ptr<TLSCtx> ctx):
    _do_send(false),
    _sender(sender),
    _receiver(receiver),
    _ctx(ctx) {
    _alloter = std::make_shared<common::BlockMemoryPool>(1500, 5); // todo add to config
}

Processor::~Processor() {

}

void Processor::Run() {
    while (!_stop) {
        // send all data to network
        if (_do_send) {
            _do_send = false;
            ProcessSend();
        }

        // try to receive data from network
        ProcessRecv();

        // check timer and do timer task
        ProcessTimer();

        int64_t wait_time = __timer->MinTime();
        wait_time = wait_time < 0 ? 1000 : wait_time;

        std::unique_lock<std::mutex> lock(_notify_mutex);
        _notify.wait_for(lock, std::chrono::milliseconds(wait_time));
    }
}

bool Processor::HandlePacket(std::shared_ptr<INetPacket> packet) {
    common::LOG_INFO("get packet from %s", packet->GetAddress().AsString().c_str());

    uint8_t* cid;
    uint16_t len = 0;
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodeNetPakcet(packet, packets, cid, len)) {
        common::LOG_ERROR("decode packet failed");
        return false;
    }
    
    // dispatch packet
    auto cid_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    auto conn = _conn_map.find(cid_code);
    if (conn != _conn_map.end()) {
        conn->second->OnPackets(packet->GetTime(), packets);
        return true;
    }

    // if pakcet is a short header packet, but we can't find in connection map, the connection may move to other thread.
    // that may happen when ip of client changed.
    // check init packet?
    if (!InitPacketCheck(packets[0])) {
        // TODO reset connection
        return false;
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(_ctx, __timer,
        std::bind(&Processor::AddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Processor::RetireConnectionId, this, std::placeholders::_1));
    new_conn->AddRemoteConnectionId(cid, len);
    _conn_map[cid_code] = new_conn;
    new_conn->OnPackets(packet->GetTime(), packets);

    return true;
}

void Processor::ActiveSendConnection(std::shared_ptr<IConnection> conn) {
    _active_send_connection_list.push_back(conn);
    _do_send = true;
}

void Processor::WeakUp() {
     _notify.notify_one();
}

std::shared_ptr<IConnection> Processor::MakeClientConnection() {
    auto new_conn = std::make_shared<ClientConnection>(_ctx, __timer,
        std::bind(&Processor::AddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Processor::RetireConnectionId, this, std::placeholders::_1));
    return new_conn;
}

void Processor::ProcessRecv() {
    int times = __max_recv_times;
    while (times >= 0) {
        times--;

        uint8_t recv_buf[__max_v4_packet_size] = {0};
        std::shared_ptr<INetPacket> packet = std::make_shared<INetPacket>();
        auto buffer = std::make_shared<common::Buffer>(recv_buf, sizeof(recv_buf));
        packet->SetData(buffer);

        auto ret = _receiver->TryRecv(packet);
        if (ret == IReceiver::RecvResult::RR_FAILED) {
            common::LOG_ERROR("recv packet failed.");
            continue;
        }
        if (ret == IReceiver::RecvResult::RR_NO_DATA) {
            break;
        }

        HandlePacket(packet);
    }
}

void Processor::ProcessTimer() {
    __timer->TimerRun();
}

void Processor::ProcessSend() {
    static thread_local uint8_t buf[1500] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));

    std::shared_ptr<INetPacket> packet;
    for (auto iter = _active_send_connection_list.begin(); iter != _active_send_connection_list.end(); ++iter) {
        if (!(*iter)->GenerateSendData(buffer)) {
            common::LOG_ERROR("generate send data failed.");
            continue;
        }

        packet->SetData(buffer);
        packet->SetSocket((*iter)->GetSock());
        packet->SetAddress((*iter)->GetPeerAddress());

        if (!_sender->Send(packet)) {
            common::LOG_ERROR("udp send failed.");
        }
        buffer->Clear();
    }
    _active_send_connection_list.clear();
}

void Processor::AddConnectionId(uint64_t cid_hash, std::shared_ptr<IConnection> conn) {
    _conn_map[cid_hash] = conn;
}

void Processor::RetireConnectionId(uint64_t cid_hash) {
    _conn_map.erase(cid_hash);
}

bool Processor::InitPacketCheck(std::shared_ptr<IPacket> packet) {
    if (packet->GetHeader()->GetPacketType() != PT_INITIAL) {
        common::LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    // TODO check packet length

    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    uint32_t version = ((LongHeader*)init_packet->GetHeader())->GetVersion();
    if (!VersionCheck(version)) {
        // TODO may generate a version negotiation packet
        return false;
    }

    return true;
}


bool Processor::DecodeNetPakcet(std::shared_ptr<INetPacket> net_packet,
    std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len) {
    if(!DecodePackets(net_packet->GetData(), packets)) {
        // todo send version negotiate packet
        return false;
    }

    if (packets.empty()) {
        common::LOG_ERROR("parse packet list is empty.");
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
}