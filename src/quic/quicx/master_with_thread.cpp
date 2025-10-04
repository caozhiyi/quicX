#include "quic/quicx/master_with_thread.h"

namespace quicx {
namespace quic {

MasterWithThread::MasterWithThread(bool ecn_enabled):
    Master(common::MakeEventLoop(), ecn_enabled) {
    
}

MasterWithThread::~MasterWithThread() {

}

void MasterWithThread::Run() {
    event_loop_->AddFixedProcess(std::bind(&MasterWithThread::Process, this));
    while (!IsStop()) {
        event_loop_->Wait();
    }
}

void MasterWithThread::Stop() {
    Thread::Stop();
    event_loop_->Wakeup();
}

void MasterWithThread::AddConnectionID(ConnectionID& cid, const std::string& worker_id) {
    connection_op_queue_.Push({ADD_CONNECTION_ID, cid, worker_id});
    event_loop_->Wakeup();
}

void MasterWithThread::RetireConnectionID(ConnectionID& cid, const std::string& worker_id) {
    connection_op_queue_.Push({RETIRE_CONNECTION_ID, cid, worker_id});
    event_loop_->Wakeup();
}

void MasterWithThread::Process() {
    Master::Process();
    DoUpdateConnectionID();
}

void MasterWithThread::DoUpdateConnectionID() {
    ConnectionOpInfo op_info;
    while (connection_op_queue_.Pop(op_info)) {
        if (op_info.operation_ == ADD_CONNECTION_ID) {
            cid_worker_map_[op_info.cid_.Hash()] = op_info.worker_id_;
        } else if (op_info.operation_ == RETIRE_CONNECTION_ID) {
            cid_worker_map_.erase(op_info.cid_.Hash());
        }
    }
}

std::shared_ptr<common::IEventLoop> MasterWithThread::GetEventLoop() {
    return event_loop_;
}
}
}