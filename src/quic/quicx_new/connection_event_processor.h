#ifndef QUIC_QUICX_NEW_CONNECTION_EVENT_PROCESSOR
#define QUIC_QUICX_NEW_CONNECTION_EVENT_PROCESSOR

#include <memory>
#include <thread>
#include <atomic>
#include "common/structure/thread_safe_block_queue.h"
#include "quic/quicx_new/connection_event.h"
#include "quic/quicx_new/connection_dispatcher.h"

namespace quicx {
namespace quic {

class ConnectionEventProcessor {
public:
    ConnectionEventProcessor(std::shared_ptr<ConnectionDispatcher> dispatcher);
    ~ConnectionEventProcessor();
    
    void Start();
    void Stop();
    void SetEventQueue(std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> queue);

private:
    void EventLoop();
    void ProcessEvent(std::shared_ptr<ConnectionEvent> event);
    
    std::shared_ptr<ConnectionDispatcher> dispatcher_;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> event_queue_;
    std::thread event_thread_;
    std::atomic<bool> running_;
};

}
}

#endif 