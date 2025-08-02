#include <iostream>
#include <thread>
#include <chrono>
#include "common/log/log.h"
#include "upgrade/network/if_event_driver.h"


int main() {
    // Create event driver for current platform
    auto event_driver = quicx::upgrade::IEventDriver::Create();
    
    if (!event_driver) {
        std::cerr << "Failed to create event driver for current platform" << std::endl;
        return 1;
    }
    
    // Initialize event driver
    if (!event_driver->Init()) {
        std::cerr << "Failed to initialize event driver" << std::endl;
        return 1;
    }
    
    std::cout << "Event driver initialized successfully" << std::endl;
    
    // Example: Add some file descriptors to monitor
    // In a real application, these would be socket file descriptors
    
    // Example event loop using Wait method
    std::vector<quicx::upgrade::Event> events;
    events.reserve(event_driver->GetMaxEvents());
    
    std::cout << "Starting event loop (will run for 5 seconds)..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        // Wait for events with 1 second timeout
        int nfds = event_driver->Wait(events, 1000);
        
        if (nfds < 0) {
            std::cerr << "Event driver wait failed" << std::endl;
            break;
        }
        
        if (nfds > 0) {
            std::cout << "Received " << nfds << " events:" << std::endl;
            for (const auto& event : events) {
                std::string event_type_str;
                switch (event.type) {
                    case quicx::upgrade::EventType::READ:
                        event_type_str = "READ";
                        break;
                    case quicx::upgrade::EventType::WRITE:
                        event_type_str = "WRITE";
                        break;
                    case quicx::upgrade::EventType::ERROR:
                        event_type_str = "ERROR";
                        break;
                    case quicx::upgrade::EventType::CLOSE:
                        event_type_str = "CLOSE";
                        break;
                }
                
                quicx::common::LOG_INFO("Event: fd=%d, type=%s", event.fd, event_type_str.c_str());
            }
        } else {
            std::cout << "No events received (timeout)" << std::endl;
        }
    }
    
    std::cout << "Event driver example completed" << std::endl;
    return 0;
} 