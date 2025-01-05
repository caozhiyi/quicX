#include "common/log/log.h"
#include "quic/quicx/multi_processor.h"
#include "quic/quicx/connection_transfor.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {


std::unordered_map<std::thread::id, MultiProcessor*> MultiProcessor::processor_map__;

MultiProcessor::MultiProcessor(std::shared_ptr<TLSCtx> ctx,
    std::function<void(std::shared_ptr<IConnection>)> connection_handler):
    Processor(ctx, connection_handler) {
}

MultiProcessor::~MultiProcessor() {

}

void MultiProcessor::Run() {
    // register processor in woker thread
    processor_map__[std::this_thread::get_id()] = this;
    connection_transfor_ = std::make_shared<ConnectionTransfor>();
    while (!IsStop()) {
        Processor::Process();

        while (GetQueueSize() > 0) {
            auto func = Pop();
            func();
        }
    }
}

void MultiProcessor::Stop() {
    Thread::Stop();
    Weakeup();
}

void MultiProcessor::Weakeup() {
    receiver_->Weakup();
}

bool MultiProcessor::HandlePacket(std::shared_ptr<INetPacket> packet) {
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

    // if pakcet is a short header packet, but we can't find in connection map, the connection may exist in other thread.
    // that may happen when ip of client changed.
    auto pkt_type = packets[0]->GetHeader()->GetPacketType();
    if (pkt_type == PacketType::PT_1RTT && connection_transfor_) {
        connection_transfor_->TryCatchConnection(cid_code);
        return true;
    }
    
    // check init packet?
    if (!InitPacketCheck(packets[0])) {
        // TODO reset connection
        return false;
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(ctx_, time_,
        std::bind(&MultiProcessor::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&MultiProcessor::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&MultiProcessor::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&MultiProcessor::HandleRetireConnectionId, this, std::placeholders::_1));

    new_conn->AddRemoteConnectionId(cid, len);
    new_conn->OnPackets(packet->GetTime(), packets);

    conn_map_[cid_code] = new_conn;
    return true;
}

void MultiProcessor::TransferConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) {
    conn->SetTimer(Processor::time_);
    conn->SetActiveConnectionCB(std::bind(&MultiProcessor::HandleActiveSendConnection, this, std::placeholders::_1));
    conn->SetHandshakeDoneCB(std::bind(&MultiProcessor::HandleHandshakeDone, this, std::placeholders::_1));
    conn->SetAddConnectionIdCB(std::bind(&MultiProcessor::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2));
    conn->SetRetireConnectionIdCB(std::bind(&MultiProcessor::HandleRetireConnectionId, this, std::placeholders::_1));
    conn_map_[cid_hash] = conn;
}

void MultiProcessor::ConnectionIDNoexist(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) {
    // do nothing
}

void MultiProcessor::CatchConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) {
    // return the connection to outside
    auto iter = conn_map_.find(cid_hash);
    if (iter != conn_map_.end()) {
        conn = iter->second;
    }
    // remove from map
    conn_map_.erase(iter);
}

}
}