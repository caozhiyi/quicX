#include "common/log/log.h"
#include "quic/quicx_new/connection_event_processor.h"

namespace quicx {
namespace quic {

ConnectionEventProcessor::ConnectionEventProcessor(std::shared_ptr<ConnectionDispatcher> dispatcher)
    : dispatcher_(dispatcher), event_queue_(nullptr), running_(false) {
}

ConnectionEventProcessor::~ConnectionEventProcessor() {
    Stop();
}

void ConnectionEventProcessor::Start() {
    if (running_.load()) {
        common::LOG_WARN("ConnectionEventProcessor is already running");
        return;
    }
    
    if (!event_queue_) {
        common::LOG_ERROR("Event queue not set, cannot start processor");
        return;
    }
    
    running_.store(true);
    event_thread_ = std::thread(&ConnectionEventProcessor::EventLoop, this);
    common::LOG_INFO("ConnectionEventProcessor started");
}

void ConnectionEventProcessor::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    
    common::LOG_INFO("ConnectionEventProcessor stopped");
}

void ConnectionEventProcessor::SetEventQueue(std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> queue) {
    event_queue_ = queue;
}

void ConnectionEventProcessor::EventLoop() {
    common::LOG_DEBUG("ConnectionEventProcessor event loop started");
    
    while (running_.load()) {
        std::shared_ptr<ConnectionEvent> event;
        if (event_queue_->TryPop(event, std::chrono::milliseconds(100))) {
            ProcessEvent(event);
        }
    }
    
    common::LOG_DEBUG("ConnectionEventProcessor event loop stopped");
}

void ConnectionEventProcessor::ProcessEvent(std::shared_ptr<ConnectionEvent> event) {
    if (!event) {
        common::LOG_WARN("Received null connection event");
        return;
    }
    
    try {
        dispatcher_->ProcessConnectionEvent(event);
    } catch (const std::exception& e) {
        common::LOG_ERROR("Exception while processing connection event: %s", e.what());
    }
}

}
} 