#include "quic/quicx/master_with_thread.h"

#include <future>

#include "common/log/log.h"

namespace quicx {
namespace quic {

MasterWithThread::MasterWithThread(bool ecn_enabled, std::shared_ptr<common::IEventLoop> event_loop):
    Master(ecn_enabled, event_loop),
    event_loop_(event_loop) {}

MasterWithThread::~MasterWithThread() {}

void MasterWithThread::Run() {
    auto loop = event_loop_.lock();
    if (!loop) {
        common::LOG_ERROR("event loop expired.");
        return;
    }

    if (!loop->Init()) {
        common::LOG_ERROR("init event loop failed.");
        return;
    }

    Master::Init();

    loop->AddFixedProcess(shared_from_this(), std::bind(&MasterWithThread::Process, this));

    // Process any tasks that were posted before EventLoop was initialized
    std::function<void()> task;
    while (pending_tasks_.Pop(task)) {
        loop->PostTask(std::move(task));
    }

    while (!IsStop()) {
        loop->Wait();
    }
}

void MasterWithThread::Stop() {
    Thread::Stop();
    auto loop = event_loop_.lock();
    if (loop) {
        loop->Wakeup();
    }
}

void MasterWithThread::AddConnectionID(ConnectionID& cid, const std::string& worker_id) {
    connection_op_queue_.Push({ADD_CONNECTION_ID, cid, worker_id});
    auto loop = event_loop_.lock();
    if (loop) {
        loop->Wakeup();
    }
}

void MasterWithThread::RetireConnectionID(ConnectionID& cid, const std::string& worker_id) {
    connection_op_queue_.Push({RETIRE_CONNECTION_ID, cid, worker_id});
    auto loop = event_loop_.lock();
    if (loop) {
        loop->Wakeup();
    }
}

void MasterWithThread::Process() {
    Master::Process();
    DoUpdateConnectionID();
}

void MasterWithThread::PostTask(std::function<void()> task) {
    auto loop = event_loop_.lock();
    if (loop) {
        loop->PostTask(std::move(task));
    } else {
        // Queue task if EventLoop is not yet initialized
        pending_tasks_.Push(std::move(task));
    }
}

bool MasterWithThread::AddListener(int32_t listener_sock) {
    auto loop = event_loop_.lock();
    common::LOG_DEBUG("MasterWithThread::AddListener called: fd=%d, event_loop=%p", listener_sock, loop.get());

    // If EventLoop is initialized, synchronously register the listener on the
    // master-loop thread before returning. This guarantees that by the time
    // ListenAndAccept() returns to the caller, the UDP socket is already bound
    // and armed in the event driver, so the very first client packet cannot be
    // silently dropped while the listener is "in flight".
    if (loop) {
        // Fast path: already on loop thread -> register directly and return real result.
        if (loop->IsInLoopThread()) {
            return receiver_->AddReceiver(listener_sock, shared_from_this());
        }

        std::promise<bool> done;
        std::future<bool> fut = done.get_future();
        loop->RunInLoop([this, listener_sock, &done]() {
            bool ok = receiver_->AddReceiver(listener_sock, shared_from_this());
            done.set_value(ok);
        });
        return fut.get();
    }

    // If EventLoop is not initialized yet, add directly to pending_listeners_
    // This will be processed in Master::Init() when Run() starts
    common::LOG_DEBUG(
        "MasterWithThread::AddListener: EventLoop not initialized, adding to pending list for fd=%d", listener_sock);
    ListenerInfo info;
    info.sock = listener_sock;
    pending_listeners_.push_back(info);
    return true;
}

bool MasterWithThread::AddListener(const std::string& ip, uint16_t port) {
    // If EventLoop is initialized, synchronously register the listener on the
    // master-loop thread (see AddListener(int32_t) above for rationale).
    auto loop = event_loop_.lock();
    if (loop) {
        if (loop->IsInLoopThread()) {
            return receiver_->AddReceiver(ip, port, shared_from_this());
        }

        std::promise<bool> done;
        std::future<bool> fut = done.get_future();
        loop->RunInLoop([this, ip, port, &done]() {
            bool ok = receiver_->AddReceiver(ip, port, shared_from_this());
            done.set_value(ok);
        });
        return fut.get();
    }

    // If EventLoop is not initialized yet, add directly to pending_listeners_
    // This will be processed in Master::Init() when Run() starts
    ListenerInfo info;
    info.ip = ip;
    info.port = port;
    pending_listeners_.push_back(info);
    return true;
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