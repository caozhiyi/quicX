#ifndef QUIC_QUICX_MULTI_PROCESSOR
#define QUIC_QUICX_MULTI_PROCESSOR

#include <mutex>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "quic/quicx/if_processor.h"
#include "quic/connection/if_connection.h"
#include "common/thread/thread_with_queue.h"

namespace quicx {
namespace quic {

/*
 multi processor, supports multiple threads
*/
class ConnectionTransfor;
class ThreadProcessor:
    public IProcessor,
    public common::ThreadWithQueue<std::function<void()>> {
public:
    ThreadProcessor();
    virtual ~ThreadProcessor();

    virtual void Run();

    virtual void Stop();

    void Weakup();

    std::thread::id GetCurrentThreadId();
    
protected:
    // transfer a connection from other processor
    virtual void TransferConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) = 0;
    // all threads can't find the connection
    void ConnectionIDNoexist(uint64_t cid_hash, std::shared_ptr<IConnection>& conn);
    // catch a connection from local map if there is target conn
    void CatchConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn);

protected:
    bool wait_close_;
    std::shared_ptr<ISender> sender_;
    std::shared_ptr<IReceiver> receiver_;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> conn_map_; // all connections

    friend class ConnectionTransfor;
    std::shared_ptr<ConnectionTransfor> connection_transfor_;
    static std::unordered_map<std::thread::id, ThreadProcessor*> s_processor_map;
    std::thread::id current_thread_id_;

    bool current_thread_id_set_;
    std::mutex current_thread_id_mutex_;
    std::condition_variable current_thread_id_cv_;
};

}
}

#endif