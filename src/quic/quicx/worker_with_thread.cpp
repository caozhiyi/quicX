#include <sstream>
#include "quic/quicx/worker_with_thread.h"
#include "quic/quicx/worker.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

WorkerWithThread::WorkerWithThread(std::shared_ptr<IWorker> worker_ptr):
    worker_ptr_(worker_ptr) {}

WorkerWithThread::~WorkerWithThread() {}

std::string WorkerWithThread::GetWorkerId() {
    if (worker_id_.empty() && pthread_) {
        std::ostringstream oss;
        oss << pthread_->get_id();
        worker_id_ = oss.str();
    }
    return worker_id_;
}

// Handle packets
void WorkerWithThread::HandlePacket(PacketParseResult& packet_info) {
    packet_queue_.Emplace(std::move(packet_info));
    if (event_loop_) {
        event_loop_->Wakeup();
    }
}

void WorkerWithThread::Run() {
    // Save EventLoop reference for cross-thread access
    event_loop_ = GlobalResource::Instance().GetThreadLocalEventLoop();
    // Also set EventLoop in the underlying worker for HandleActiveSendConnection
    if (worker_ptr_) {
        auto worker = std::dynamic_pointer_cast<Worker>(worker_ptr_);
        if (worker) {
            worker->SetEventLoop(event_loop_);
        }
    }
    
    while (!Thread::IsStop()) {
        event_loop_->Wait();
        ProcessRecv();
    }
}

void WorkerWithThread::Stop() {
    Thread::Stop();
    if (event_loop_) {
        event_loop_->Wakeup();
    }
}

void WorkerWithThread::PostTask(std::function<void()> task) {
    if (event_loop_) {
        event_loop_->PostTask(std::move(task));
    }
}

std::shared_ptr<common::IEventLoop> WorkerWithThread::GetEventLoop() {
    return event_loop_;
}

void WorkerWithThread::ProcessRecv() {
    PacketParseResult packet_info;
    if (packet_queue_.TryPop(packet_info)) {
        worker_ptr_->HandlePacket(packet_info);
    }
}

}  // namespace quic
}  // namespace quicx