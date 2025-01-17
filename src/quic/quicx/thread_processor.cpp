#include "common/log/log.h"
#include "quic/udp/udp_sender.h"
#include "quic/udp/udp_receiver.h"
#include "quic/quicx/thread_processor.h"
#include "quic/quicx/connection_transfor.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {


std::unordered_map<std::thread::id, ThreadProcessor*> ThreadProcessor::processor_map__;

ThreadProcessor::ThreadProcessor() {
    receiver_ = std::make_shared<UdpReceiver>();
    sender_ = std::make_shared<UdpSender>();
    receiver_->AddReceiver(sender_->GetSocket());
}

ThreadProcessor::~ThreadProcessor() {

}

void ThreadProcessor::Run() {
    // register processor in woker thread
    processor_map__[std::this_thread::get_id()] = this;
    connection_transfor_ = std::make_shared<ConnectionTransfor>();
    while (!IsStop()) {
        Process();

        while (GetQueueSize() > 0) {
            auto func = Pop();
            func();
        }
    }
}

void ThreadProcessor::Stop() {
    // close all connections
    for (auto& conn : conn_map_) {
        conn.second->Close();
    }

    // TODO: wait all connections closed
    Thread::Stop();
    Weakeup();
}

void ThreadProcessor::Weakeup() {
    receiver_->Weakup();
}

void ThreadProcessor::ConnectionIDNoexist(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) {
    // do nothing
}

void ThreadProcessor::CatchConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) {
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