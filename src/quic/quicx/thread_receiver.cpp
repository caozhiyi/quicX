#include "common/log/log.h"
#include "common/util/time.h"
#include "common/buffer/buffer.h"
#include "quic/quicx/thread_receiver.h"

namespace quicx {

thread_local std::vector<std::thread::id> ThreadReceiver::_thread_vec;
thread_local std::unordered_map<uint64_t, std::thread::id> ThreadReceiver::_thread_map;
thread_local std::unordered_map<std::thread::id, ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>*> ThreadReceiver::_processer_map;

std::shared_ptr<UdpPacketIn> ThreadReceiver::DoRecv() {
    auto id = std::this_thread::get_id();
    static thread_local std::shared_ptr<ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>> __queue;
    if (!__queue) {
        __queue = std::make_shared<ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>>();
        auto queue = __queue.get();
        Push([&id, &queue, this] { RegisteThread(id, queue); });
    }

    if (!__queue->Empty()) {
        return __queue->Pop();
    }
    
    return nullptr;
}

bool ThreadReceiver::Listen(const std::string& ip, uint16_t port) {
    if (!Receiver::Listen(ip, port)) {
        return false;   
    }
    Start();
    return true;
}

void ThreadReceiver::RegisteConnection(std::thread::id id, uint64_t cid_code) {
    auto local_id = std::this_thread::get_id();
    if (local_id == id) {
        _thread_map[cid_code] = id;

    } else {
        Push([&id, &cid_code, this] { RegisteConnection(id, cid_code); });
    }
}

void ThreadReceiver::CancelConnection(std::thread::id id, uint64_t cid_code) {
    auto local_id = std::this_thread::get_id();
    if (local_id == id) {
        _thread_map.erase(cid_code);

    } else {
        Push([&id, &cid_code, this] { CancelConnection(id, cid_code); });
    }
}

void ThreadReceiver::WeakUp() {
    Push(nullptr);
}

void ThreadReceiver::Run() {
    // only recv thead
    while (!_stop) {
        // todo block io to recv
        auto packet = Receiver::DoRecv();
        if (packet != nullptr) {
            DispatcherPacket(packet);
        }
        
        while (!_queue.Empty()) {
            auto task = Pop();
            if (task) {
                task();
            }
        }

        Sleep(100);
    }
}

void ThreadReceiver::DispatcherPacket(std::shared_ptr<UdpPacketIn> packet) {
    if (!packet->DecodePacket()) {
        LOG_ERROR("decode packet failed.");
        return;
    }
    
    // find a connection
    uint64_t hash_code = packet->GetConnectionHashCode();
    auto thread_iter = _thread_map.find(hash_code);
    if (thread_iter != _thread_map.end()) {
        auto processer_iter = _processer_map.find(thread_iter->second);
        if (processer_iter != _processer_map.end()) {
            processer_iter->second->Push(packet);
        }
        return;
    }
    
    // a new connection
    static uint32_t index = 0; // TODO random
    index = index >= _thread_vec.size() ? index - _thread_vec.size() : index;
    auto id = _thread_vec[index];
    _thread_map[hash_code] = id;
    auto processer_iter = _processer_map.find(id);
    if (processer_iter != _processer_map.end()) {
        processer_iter->second->Push(packet);
    }
    index++;
}

void ThreadReceiver::RegisteThread(std::thread::id& id, ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>* queue) {
    _thread_vec.push_back(id);
    _processer_map[id] = queue;
}

}
