#include "common/log/log.h"
#include "quic/quicx/master_with_thread.h"


namespace quicx {
namespace quic {

MasterWithThread::MasterWithThread(bool ecn_enabled, std::shared_ptr<common::IEventLoop> event_loop):
    Master(ecn_enabled, event_loop),
    event_loop_(event_loop) {
}

MasterWithThread::~MasterWithThread() {
}

void MasterWithThread::Run() {
    event_loop_->Init();
    if (!event_loop_->Init()) {
        common::LOG_ERROR("init event loop failed.");
        return;
    }

    Master::Init();

    event_loop_->AddFixedProcess(std::bind(&MasterWithThread::Process, this));
    
    // Process any tasks that were posted before EventLoop was initialized
    std::function<void()> task;
    while (pending_tasks_.Pop(task)) {
        event_loop_->PostTask(std::move(task));
    }
    
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
    } else {
        // Queue task if EventLoop is not yet initialized
        pending_tasks_.Push(std::move(task));
    }
}

bool MasterWithThread::AddListener(int32_t listener_sock) {
    common::LOG_DEBUG("MasterWithThread::AddListener called: fd=%d, event_loop=%p", 
                      listener_sock, event_loop_.get());
    
    // If EventLoop is initialized, use RunInLoop to ensure immediate execution if already in loop thread
    if (event_loop_) {
        common::LOG_DEBUG("MasterWithThread::AddListener: calling RunInLoop for fd=%d", listener_sock);
        event_loop_->RunInLoop([this, listener_sock]() {
            common::LOG_DEBUG("MasterWithThread::AddListener: inside RunInLoop lambda for fd=%d", listener_sock);
            receiver_->AddReceiver(listener_sock, shared_from_this());
        });
        common::LOG_DEBUG("MasterWithThread::AddListener: RunInLoop returned for fd=%d", listener_sock);
        return true;
    }
    
    // If EventLoop is not initialized yet, add directly to pending_listeners_
    // This will be processed in Master::Init() when Run() starts
    common::LOG_DEBUG("MasterWithThread::AddListener: EventLoop not initialized, adding to pending list for fd=%d", listener_sock);
    ListenerInfo info;
    info.sock = listener_sock;
    pending_listeners_.push_back(info);
    return true;
}

bool MasterWithThread::AddListener(const std::string& ip, uint16_t port) {
    // If EventLoop is initialized, post task to Master thread's EventLoop
    if (event_loop_) {
        event_loop_->RunInLoop([this, ip, port]() {
            receiver_->AddReceiver(ip, port, shared_from_this());
        });
        return true;
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