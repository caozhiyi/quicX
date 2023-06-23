#ifndef QUIC_PROCESS_THREAD_RECEIVER
#define QUIC_PROCESS_THREAD_RECEIVER

#include <thread>
#include <unordered_map>
#include "quic/process/receiver.h"
#include "common/thread/thread_with_queue.h"

namespace quicx {

typedef std::function<void()> Task;

class ThreadReceiver:
    public Receiver,
    public ThreadWithQueue<Task> {
public:
    ThreadReceiver() {}
    virtual ~ThreadReceiver() {}

    virtual bool Listen(const std::string& ip, uint16_t port);

    virtual std::shared_ptr<UdpPacketIn> DoRecv();

protected:
    virtual void Run();
    void DispatcherPacket(std::shared_ptr<UdpPacketIn> packet);
    void RegisteThread(std::thread::id& id, ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>* queue);
    void RegisteConnection(std::thread::id& id, uint64_t cid_code);
    void CancelConnection(std::thread::id& id, uint64_t cid_code);

private:
    static thread_local std::vector<std::thread::id> _thread_vec;
    static thread_local std::unordered_map<uint64_t, std::thread::id> _thread_map;
    static thread_local std::unordered_map<std::thread::id, ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>*> _processer_map;
};

}

#endif