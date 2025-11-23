#include <sstream>
#include "common/log/log.h"
#include "common/network/if_event_loop.h"
#include "quic/quicx/worker_with_thread.h"


namespace quicx {
namespace quic {

WorkerWithThread::WorkerWithThread(std::shared_ptr<common::IEventLoop> event_loop, std::shared_ptr<IWorker> worker_ptr):
    event_loop_(event_loop),
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
    event_loop_->Init();
    if (!event_loop_->Init()) {
        common::LOG_ERROR("init event loop failed.");
        return;
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

void WorkerWithThread::ProcessRecv() {
    PacketParseResult packet_info;
    if (packet_queue_.TryPop(packet_info)) {
        if (worker_ptr_) {
            worker_ptr_->HandlePacket(packet_info);
        } else {
            common::LOG_ERROR("worker_ptr_ is not set.");
        }
    }
}

}  // namespace quic
}  // namespace quicx