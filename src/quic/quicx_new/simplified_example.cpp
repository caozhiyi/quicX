#include "quic/quicx_new/simplified_connection_manager_factory.h"
#include "quic/quicx_new/simplified_worker_manager.h"
#include "common/log/log.h"
#include <thread>
#include <chrono>

namespace quicx {
namespace quic {

// Example usage of the simplified connection manager system
void SimplifiedExample() {
    common::LOG_INFO("Starting simplified connection manager example");
    
    // Create simplified connection manager with 4 workers
    auto connection_manager = SimplifiedConnectionManagerFactory::Create(4);
    
    // Create worker manager
    auto worker_manager = std::make_shared<SimplifiedWorkerManager>(connection_manager, 4);
    
    // Start the connection manager
    connection_manager->Start();
    
    // Start worker manager
    worker_manager->Start();
    
    // Add UDP receiver
    auto simplified_manager = std::dynamic_pointer_cast<SimplifiedConnectionManager>(connection_manager);
    if (simplified_manager) {
        simplified_manager->AddReceiver("0.0.0.0", 8080);
    }
    
    // Simulate some work
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Stop everything
    worker_manager->Stop();
    connection_manager->Stop();
    
    common::LOG_INFO("Simplified connection manager example completed");
}

// Example of how worker threads can send connection events
void WorkerThreadExample(std::shared_ptr<IConnectionManager> manager) {
    auto event_queue = manager->GetEventQueue();
    if (!event_queue) {
        common::LOG_ERROR("Event queue not available");
        return;
    }
    
    // Worker thread adding a new connection ID
    auto add_event = std::make_shared<ConnectionEvent>(
        ConnectionEvent::EventType::ADD_CONNECTION_ID,
        0x1234567890ABCDEF,  // Connection ID hash
        std::this_thread::get_id()
    );
    event_queue->Push(add_event);
    
    // Worker thread removing a connection ID
    auto remove_event = std::make_shared<ConnectionEvent>(
        ConnectionEvent::EventType::REMOVE_CONNECTION_ID,
        0x1234567890ABCDEF,  // Connection ID hash
        std::this_thread::get_id()
    );
    event_queue->Push(remove_event);
    
    common::LOG_DEBUG("Worker thread sent connection events");
}

}
} 