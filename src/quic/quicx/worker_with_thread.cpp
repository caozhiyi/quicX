#include <sstream>
#include "quic/quicx/worker_with_thread.h"

namespace quicx {
namespace quic {

WorkerWithThread::WorkerWithThread(std::unique_ptr<IWorker> worker_ptr):
    worker_ptr_(std::move(worker_ptr)) {

}

WorkerWithThread::~WorkerWithThread() {

}

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
    worker_ptr_->GetEventLoop()->Wakeup();
}

std::shared_ptr<common::IEventLoop> WorkerWithThread::GetEventLoop() {
    return worker_ptr_->GetEventLoop();
}

void WorkerWithThread::Run() {
    while (!Thread::IsStop()) {
        worker_ptr_->GetEventLoop()->Wait();
        ProcessRecv();
    }
}

void WorkerWithThread::Stop() {
    Thread::Stop();
    worker_ptr_->GetEventLoop()->Wakeup();
}

void WorkerWithThread::ProcessRecv() {
    PacketParseResult packet_info;
    if (packet_queue_.TryPop(packet_info)) {
        worker_ptr_->HandlePacket(packet_info);
    }
}

}
}