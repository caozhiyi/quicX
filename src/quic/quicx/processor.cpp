#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/udp/udp_sender.h"
#include "quic/quicx/processor.h"
#include "quic/udp/udp_receiver.h"
#include "quic/common/constants.h"
#include "common/network/address.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

thread_local std::shared_ptr<common::ITimer> Processor::time_ = common::MakeTimer();

Processor::Processor(std::shared_ptr<TLSCtx> ctx,
    connection_state_callback connection_handler):
    do_send_(false),
    ctx_(ctx),
    connection_handler_(connection_handler) {
    alloter_ = std::make_shared<common::BlockMemoryPool>(1500, 5); // todo add to config

    receiver_ = std::make_shared<UdpReceiver>();
    sender_ = std::make_shared<UdpSender>();
}

Processor::~Processor() {

}

void Processor::Process() {
    // send all data to network
    if (do_send_) {
        do_send_ = false;
        ProcessSend();
    }

    int64_t waittime_ = time_->MinTime();
    waittime_ = waittime_ < 0 ? 1000 : waittime_;

    // try to receive data from network
    ProcessRecv(waittime_);

    // check timer and do timer task
    ProcessTimer();
}

void Processor::AddReceiver(uint64_t socket_fd) {
    receiver_->AddReceiver(socket_fd);
}

void Processor::AddReceiver(const std::string& ip, uint16_t port) {
    receiver_->AddReceiver(ip, port);
}

void Processor::Connect(const std::string& ip, uint16_t port) {
    auto conn = std::make_shared<ClientConnection>(ctx_, time_,
        std::bind(&Processor::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&Processor::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&Processor::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Processor::HandleRetireConnectionId, this, std::placeholders::_1));
    
    connecting_map_[conn->GetConnectionIDHash()] = conn;
    conn->Dial(common::Address(ip, port));
    // TODO add timer to check connection status
}

void Processor::ProcessRecv(uint32_t timeout_ms) {
    uint8_t recv_buf[__max_v4_packet_size] = {0};
    std::shared_ptr<INetPacket> packet = std::make_shared<INetPacket>();
    auto buffer = std::make_shared<common::Buffer>(recv_buf, sizeof(recv_buf));
    packet->SetData(buffer);

    receiver_->TryRecv(packet, timeout_ms);

    // if wakeup by timer, there may be no packet.
    if (packet->GetData()->GetDataLength() > 0) {
        HandlePacket(packet);
    }
}

void Processor::ProcessTimer() {
    time_->TimerRun();
}

void Processor::ProcessSend() {
    static thread_local uint8_t buf[1500] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));

    std::shared_ptr<INetPacket> packet;
    for (auto iter = active_send_connection_list_.begin(); iter != active_send_connection_list_.end(); ++iter) {
        if (!(*iter)->GenerateSendData(buffer)) {
            common::LOG_ERROR("generate send data failed.");
            continue;
        }

        packet->SetData(buffer);
        packet->SetAddress((*iter)->GetPeerAddress());

        if (!sender_->Send(packet)) {
            common::LOG_ERROR("udp send failed.");
        }
        buffer->Clear();
    }
    active_send_connection_list_.clear();
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
    auto conn = conn_map_.find(cid_code);
    if (conn != conn_map_.end()) {
        conn->second->OnPackets(packet->GetTime(), packets);
        return true;
    }

    // check init packet?
    if (!InitPacketCheck(packets[0])) {
        // TODO reset connection
        return false;
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(ctx_, time_,
        std::bind(&Processor::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&Processor::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&Processor::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Processor::HandleRetireConnectionId, this, std::placeholders::_1));
    connecting_map_[cid_code] = new_conn;
    // TODO add timer to check connection status

    new_conn->AddRemoteConnectionId(cid, len);
    new_conn->OnPackets(packet->GetTime(), packets);
    return true;
}

void Processor::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    connecting_map_.erase(conn->GetConnectionIDHash());
    conn_map_[conn->GetConnectionIDHash()] = conn;
    connection_handler_(conn, 0);
}

void Processor::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    active_send_connection_list_.push_back(conn);
    do_send_ = true;
}

void Processor::HandleAddConnectionId(uint64_t cid_hash, std::shared_ptr<IConnection> conn) {
    conn_map_[cid_hash] = conn;
}

void Processor::HandleRetireConnectionId(uint64_t cid_hash) {
    conn_map_.erase(cid_hash);
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
        // TODO send version negotiate packet
        return false;
    }

    if (packets.empty()) {
        common::LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    auto first_packet_header = packets[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PHT_SHORT_HEADER) {
        // TODO get short header dcid
    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        len = long_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)long_header->GetDestinationConnectionId();
    }
    return true;
}

}
}