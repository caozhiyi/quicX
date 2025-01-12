#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/udp/udp_sender.h"
#include "quic/quicx/processor.h"
#include "quic/udp/udp_receiver.h"
#include "quic/common/constants.h"
#include "common/network/address.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/header/long_header.h"
#include "quic/packet/header/short_header.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/server_connection.h"
#include "quic/packet/version_negotiation_packet.h"
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
    receiver_->AddReceiver(sender_->GetSocket());
}

Processor::~Processor() {

}

void Processor::Process() {
    // send all data to network
    if (do_send_) {
        ProcessSend();
        do_send_ = active_send_connection_set_.empty();
    }

    int64_t waittime_ = time_->MinTime();
    waittime_ = waittime_ < 0 ? 100000 : waittime_;

    // try to receive data from network
    ProcessRecv(waittime_);

    // check timer and do timer task
    ProcessTimer();
}

void Processor::SetServerAlpn(const std::string& alpn) {
    server_alpn_ = alpn;
}

void Processor::AddReceiver(uint64_t socket_fd) {
    receiver_->AddReceiver(socket_fd);
}

void Processor::AddReceiver(const std::string& ip, uint16_t port) {
    receiver_->AddReceiver(ip, port);
}

void Processor::Connect(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    auto conn = std::make_shared<ClientConnection>(ctx_, time_,
        std::bind(&Processor::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&Processor::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&Processor::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Processor::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&Processor::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    connecting_map_[conn->GetConnectionIDHash()] = conn;
    conn->Dial(common::Address(ip, port), alpn);
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

    std::shared_ptr<INetPacket> packet = std::make_shared<INetPacket>();
    bool send_done = false;
    for (auto iter = active_send_connection_set_.begin(); iter != active_send_connection_set_.end();) {
        if (!(*iter)->GenerateSendData(buffer, send_done)) {
            common::LOG_ERROR("generate send data failed.");
            iter = active_send_connection_set_.erase(iter);
            continue;
        }

        packet->SetData(buffer);
        packet->SetAddress((*iter)->GetPeerAddress());

        if (!sender_->Send(packet)) {
            common::LOG_ERROR("udp send failed.");
        }
        buffer->Clear();
        if (send_done) {
            iter = active_send_connection_set_.erase(iter);
        } else {
            iter++;
        }
    }
}

bool Processor::HandlePacket(std::shared_ptr<INetPacket> packet) {
    common::LOG_INFO("get packet from %s", packet->GetAddress().AsString().c_str());

    uint8_t* cid;
    uint16_t len = 0;
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodeNetPakcet(packet, packets, cid, len)) {
        common::LOG_ERROR("decode packet failed");
        SendVersionNegotiatePacket(packet);
        return false;
    }
    
    // dispatch packet
    auto cid_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    auto conn = conn_map_.find(cid_code);
    if (conn != conn_map_.end()) {
        conn->second->OnPackets(packet->GetTime(), packets);
        return true;
    }

    // check init packet
    if (!InitPacketCheck(packets[0])) {
        common::LOG_ERROR("init packet check failed");
        SendVersionNegotiatePacket(packet);
        return false;
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(ctx_, server_alpn_, time_,
        std::bind(&Processor::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&Processor::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&Processor::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Processor::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&Processor::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    connecting_map_[cid_code] = new_conn;
    // TODO add timer to check connection status

    new_conn->SetPeerAddress(packet->GetAddress());
    new_conn->AddRemoteConnectionId(cid, len);
    new_conn->OnPackets(packet->GetTime(), packets);
    return true;
}

void Processor::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    connecting_map_.erase(conn->GetConnectionIDHash());
    conn_map_[conn->GetConnectionIDHash()] = conn;
    connection_handler_(conn, 0, "");
}

void Processor::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    active_send_connection_set_.insert(conn);
    do_send_ = true;
    receiver_->Weakup();
}

void Processor::HandleAddConnectionId(uint64_t cid_hash, std::shared_ptr<IConnection> conn) {
    conn_map_[cid_hash] = conn;
}

void Processor::HandleRetireConnectionId(uint64_t cid_hash) {
    conn_map_.erase(cid_hash);
}

void Processor::HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    conn_map_.erase(conn->GetConnectionIDHash());
    connection_handler_(conn, error, reason);
}

void Processor::SendVersionNegotiatePacket(std::shared_ptr<INetPacket> packet) {
    VersionNegotiationPacket version_negotiation_packet;
    for (auto version : __quic_versions) {
        version_negotiation_packet.AddSupportVersion(version);
    }

    uint8_t buf[1500] = {0};
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    version_negotiation_packet.Encode(buffer);

    auto net_packet = std::make_shared<INetPacket>();
    net_packet->SetData(buffer);
    net_packet->SetAddress(packet->GetAddress());
    net_packet->SetSocket(packet->GetSocket());
    sender_->Send(net_packet);
}

bool Processor::InitPacketCheck(std::shared_ptr<IPacket> packet) {
    if (packet->GetHeader()->GetPacketType() != PT_INITIAL) {
        common::LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    // check init packet length according to RFC9000
    // Initial packets MUST be sent with a payload length of at least 1200 bytes
    // unless the client knows that the server supports a larger minimum PMTU
    if (packet->GetSrcBuffer().GetLength() < 1200) {
        common::LOG_ERROR("init packet length too small. length:%d", packet->GetSrcBuffer().GetLength());
        return false;
    }

    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    uint32_t version = ((LongHeader*)init_packet->GetHeader())->GetVersion();
    if (!VersionCheck(version)) {
        return false;
    }

    return true;
}


bool Processor::DecodeNetPakcet(std::shared_ptr<INetPacket> net_packet,
    std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len) {
    if(!DecodePackets(net_packet->GetData(), packets)) {
        common::LOG_ERROR("decode packet failed");
        return false;
    }

    if (packets.empty()) {
        common::LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    auto first_packet_header = packets[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PHT_SHORT_HEADER) {
        auto short_header = dynamic_cast<ShortHeader*>(first_packet_header);
        len = short_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)short_header->GetDestinationConnectionId();

    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        len = long_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)long_header->GetDestinationConnectionId();
    }
    return true;
}

}
}