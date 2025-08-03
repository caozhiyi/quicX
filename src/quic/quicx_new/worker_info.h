#ifndef QUIC_QUICX_NEW_WORKER_INFO
#define QUIC_QUICX_NEW_WORKER_INFO

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include "common/structure/thread_safe_block_queue.h"
#include "quic/quicx_new/packet_task.h"

namespace quicx {
namespace quic {

struct WorkerInfo {
    std::thread::id worker_id;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue;
    std::atomic<uint32_t> connection_count;
    std::atomic<uint32_t> packet_count;
    std::chrono::steady_clock::time_point last_active;
    
    WorkerInfo() : worker_id(), queue(nullptr), connection_count(0), packet_count(0) {
        last_active = std::chrono::steady_clock::now();
    }
    
    WorkerInfo(std::thread::id id, std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> q)
        : worker_id(id), queue(q), connection_count(0), packet_count(0) {
        last_active = std::chrono::steady_clock::now();
    }
};

}
}

#endif 