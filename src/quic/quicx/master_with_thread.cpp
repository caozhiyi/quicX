#include "quic/quicx/master_with_thread.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

MasterWithThread::MasterWithThread(bool ecn_enabled):
    Master(ecn_enabled) {
}

MasterWithThread::~MasterWithThread() {}

void MasterWithThread::Run() {
    // Save EventLoop reference for cross-thread access
    event_loop_ = GlobalResource::Instance().GetThreadLocalEventLoop();

    Master::Init();

    event_loop_->AddFixedProcess(std::bind(&MasterWithThread::Process, this));
    while (!IsStop()) {
        event_loop_->Wait();
    }
}

void MasterWithThread::Stop() {
    Thread::Stop();
    if (event_loop_) {
        event_loop_->Wakeup();
    }
}

void MasterWithThread::AddConnectionID(ConnectionID& cid, const std::string& worker_id) {
    connection_op_queue_.Push({ADD_CONNECTION_ID, cid, worker_id});
    if (event_loop_) {
        event_loop_->Wakeup();
    }
}

void MasterWithThread::RetireConnectionID(ConnectionID& cid, const std::string& worker_id) {
    connection_op_queue_.Push({RETIRE_CONNECTION_ID, cid, worker_id});
    if (event_loop_) {
        event_loop_->Wakeup();
    }
}

void MasterWithThread::Process() {
    Master::Process();
    DoUpdateConnectionID();
}

void MasterWithThread::PostTask(std::function<void()> task) {
    if (event_loop_) {
        event_loop_->PostTask(std::move(task));
    }
}

std::shared_ptr<common::IEventLoop> MasterWithThread::GetEventLoop() {
    return event_loop_;
}

void MasterWithThread::DoUpdateConnectionID() {
    ConnectionOpInfo op_info;
    while (connection_op_queue_.Pop(op_info)) {
        if (op_info.operation_ == ADD_CONNECTION_ID) {
            Master::AddConnectionID(op_info.cid_, op_info.worker_id_);
        } else if (op_info.operation_ == RETIRE_CONNECTION_ID) {
            Master::RetireConnectionID(op_info.cid_, op_info.worker_id_);
        }
    }
}

}  // namespace quic
}  // namespace quicx