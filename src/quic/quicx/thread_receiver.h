#ifndef QUIC_QUICX_THREAD_RECEIVER
#define QUIC_QUICX_THREAD_RECEIVER

#include <thread>
#include <unordered_map>
#include "quic/quicx/receiver.h"
#include "common/thread/thread_with_queue.h"

namespace quicx {

typedef std::function<void()> Task;

class ThreadReceiver:
    public Receiver,
    public ThreadWithQueue<Task> {
public:
    ThreadReceiver() {}
    virtual ~ThreadReceiver() {}

    virtual std::shared_ptr<UdpPacketIn> DoRecv();

    virtual bool Listen(const std::string& ip, uint16_t port);

    virtual void RegisteConnection(std::thread::id id, uint64_t cid_code);
    virtual void CancelConnection(std::thread::id id, uint64_t cid_code);

    void WeakUp();
protected:
    virtual void Run();
    void DispatcherPacket(std::shared_ptr<UdpPacketIn> packet);
    void RegisteThread(std::thread::id& id, ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>* queue);

private:
    static thread_local std::vector<std::thread::id> _thread_vec;
    static thread_local std::unordered_map<uint64_t, std::thread::id> _thread_map;
    static thread_local std::unordered_map<std::thread::id, ThreadSafeBlockQueue<std::shared_ptr<UdpPacketIn>>*> _processer_map;
};

}

#endif