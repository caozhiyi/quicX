// #include "common/log/log.h"
// #include "common/util/time.h"
// #include "common/buffer/buffer.h"
// #include "quic/quicx/thread_receiver.h"

// namespace quicx {
// namespace quic {

// thread_local std::vector<std::thread::id> ThreadReceiver::thread_vec_;
// thread_local std::unordered_map<uint64_t, std::thread::id> ThreadReceiver::thread_map_;
// thread_local std::unordered_map<std::thread::id, common::ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>*> ThreadReceiver::processer_map_;

// std::shared_ptr<UdpPacketIn> ThreadReceiver::DoRecv() {
//     auto id = std::this_thread::get_id();
//     static thread_local std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>> __queue;
//     if (!__queue) {
//         __queue = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>>();
//         auto queue = __queue.get();
//         Push([&id, &queue, this] { RegisteThread(id, queue); });
//     }

//     if (!__queue->Empty()) {
//         return __queue->Pop();
//     }
    
//     return nullptr;
// }

// bool ThreadReceiver::Listen(const std::string& ip, uint16_t port) {
//     if (!Receiver::Listen(ip, port)) {
//         return false;   
//     }
//     Start();
//     return true;
// }

// void ThreadReceiver::RegisteConnection(std::thread::id id, uint64_t cid_code) {
//     auto local_id = std::this_thread::get_id();
//     if (local_id == id) {
//         thread_map_[cid_code] = id;

//     } else {
//         Push([&id, &cid_code, this] { RegisteConnection(id, cid_code); });
//     }
// }

// void ThreadReceiver::CancelConnection(std::thread::id id, uint64_t cid_code) {
//     auto local_id = std::this_thread::get_id();
//     if (local_id == id) {
//         thread_map_.erase(cid_code);

//     } else {
//         Push([&id, &cid_code, this] { CancelConnection(id, cid_code); });
//     }
// }

// void ThreadReceiver::WeakUp() {
//     Push(nullptr);
// }

// void ThreadReceiver::Run() {
//     // only recv thead
//     while (!stop_) {
//         // todo block io to recv
//         auto packet = Receiver::DoRecv();
//         if (packet != nullptr) {
//             DispatcherPacket(packet);
//         }
        
//         while (!queue_.Empty()) {
//             auto task = Pop();
//             if (task) {
//                 task();
//             }
//         }

//         common::Sleep(100);
//     }
// }

// void ThreadReceiver::DispatcherPacket(std::shared_ptr<UdpPacketIn> packet) {
//     if (!packet->DecodePacket()) {
//         common::LOG_ERROR("decode packet failed.");
//         return;
//     }
    
//     // find a connection
//     uint64_t hash_code = packet->GetConnectionHashCode();
//     auto thread_iter = thread_map_.find(hash_code);
//     if (thread_iter != thread_map_.end()) {
//         auto processer_iter = processer_map_.find(thread_iter->second);
//         if (processer_iter != processer_map_.end()) {
//             processer_iter->second->Push(packet);
//         }
//         return;
//     }
    
//     // a new connection
//     static uint32_t index = 0; // TODO random
//     index = index >= thread_vec_.size() ? index - thread_vec_.size() : index;
//     auto id = thread_vec_[index];
//     thread_map_[hash_code] = id;
//     auto processer_iter = processer_map_.find(id);
//     if (processer_iter != processer_map_.end()) {
//         processer_iter->second->Push(packet);
//     }
//     index++;
// }

// void ThreadReceiver::RegisteThread(std::thread::id& id, common::ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>* queue) {
//     thread_vec_.push_back(id);
//     processer_map_[id] = queue;
// }

// }
// }
