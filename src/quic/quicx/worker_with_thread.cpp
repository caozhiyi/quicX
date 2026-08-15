#include "quic/quicx/worker_with_thread.h"
#include <quicx/common/if_event_loop.h>
#include <sstream>
#include "common/log/log.h"
#include "quic/config.h"

namespace quicx {
namespace quic {

WorkerWithThread::WorkerWithThread(std::shared_ptr<common::IEventLoop> event_loop, std::shared_ptr<IWorker> worker_ptr):
    worker_ptr_(worker_ptr),
    event_loop_(event_loop),
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
    if (!worker_ptr_) {
        LOG_ERROR("worker_ptr_ is not set.");
        packet_queue_.Clear();
        return;
    }

    // Drain a whole batch per loop iteration, not a single packet.
    //
    // This used to pop exactly one PacketParseResult per Run() iteration, which
    // collapsed server throughput to ~1 packet per Wait() timeout under load:
    //
    //   * UdpReceiver::OnRead() pulls up to kMaxRecvBatch (64) datagrams in one
    //     go and calls HandlePacket() -> Emplace() + loop->Wakeup() for each.
    //   * Wakeup() is cross-thread here, so it writes one byte to the wakeup
    //     pipe -- but KqueueEventDriver::Wait() drains that pipe with a single
    //     read(fd, buf, 64). All 64 wakeup bytes are therefore consumed by ONE
    //     kevent() return: 64 producer wakeups collapse into 1 consumer wakeup.
    //   * EventLoop::Wait() only short-circuits its timeout for the PostTask
    //     queue; it has no visibility into packet_queue_. So after popping that
    //     single packet the worker went back to sleep for the next timer
    //     deadline (up to the 1000 ms default) with 63 packets still queued.
    //
    // The backlog then grows without bound. Observed on 2026-08-06: the server
    // serviced exactly 64 packets every ~1.3 s, so a fresh client's Initial sat
    // in the queue for ~9 s. The client PTO'd three times and gave up long
    // before the server ever looked at its packet -- which presented as
    // "server received the Initial but never replied".
    const uint32_t kDrainBudget = kMaxRecvBatch;
    PacketParseResult packet_info;
    for (uint32_t i = 0; i < kDrainBudget; ++i) {
        if (!packet_queue_.TryPop(packet_info)) {
            return;
        }
        worker_ptr_->HandlePacket(packet_info);
    }

    // Budget exhausted with packets still queued. Do NOT fall through to a
    // blocking Wait(): the wakeup bytes for those packets have already been
    // consumed, so nothing would wake us until the next timer fires. Asking the
    // loop for an immediate wakeup keeps the drain going on the next iteration
    // while still giving timers and Worker::Process() a turn in between.
    if (!packet_queue_.Empty()) {
        if (auto loop = event_loop_.lock()) {
            loop->Wakeup();
        }
    }
}

}  // namespace quic
}  // namespace quicx