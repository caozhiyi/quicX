#include <sstream>
#include "common/log/log.h"
#include <quicx/common/if_event_loop.h>
#include "quic/quicx/worker_with_thread.h"


namespace quicx {
namespace quic {

WorkerWithThread::WorkerWithThread(std::shared_ptr<common::IEventLoop> event_loop, std::shared_ptr<IWorker> worker_ptr):
    event_loop_(event_loop),
    worker_ptr_(worker_ptr),
    ready_future_(ready_promise_.get_future().share()) {}

WorkerWithThread::~WorkerWithThread() {}

bool WorkerWithThread::WaitUntilReady() {
    return ready_future_.get();
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
    if (auto loop = event_loop_.lock()) {
        loop->Wakeup();
    }
}

void WorkerWithThread::Run() {
    auto loop = event_loop_.lock();
    if (!loop || !loop->Init()) {
        LOG_ERROR("init event loop failed.");
        ready_promise_.set_value(false);
        return;
    }

    // Notify the main thread that initialization is complete
    ready_promise_.set_value(true);

    while (!Thread::IsStop()) {
        loop->Wait();
        ProcessRecv();
        if (worker_ptr_) {
            worker_ptr_->Process();
        }
    }
}

void WorkerWithThread::Stop() {
    Thread::Stop();
    if (auto loop = event_loop_.lock()) {
        loop->Wakeup();
    }
}

void WorkerWithThread::PostTask(std::function<void()> task) {
    if (auto loop = event_loop_.lock()) {
        loop->PostTask(std::move(task));
    }
}

void WorkerWithThread::ProcessRecv() {
    PacketParseResult packet_info;
    if (packet_queue_.TryPop(packet_info)) {
        if (worker_ptr_) {
            worker_ptr_->HandlePacket(packet_info);
        } else {
            LOG_ERROR("worker_ptr_ is not set.");
        }
    }
}

}  // namespace quic
}  // namespace quicx