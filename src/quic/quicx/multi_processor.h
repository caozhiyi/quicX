#ifndef QUIC_QUICX_MULTI_PROCESSOR
#define QUIC_QUICX_MULTI_PROCESSOR

#include <mutex>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include "quic/quicx/processor.h"
#include "common/thread/thread_with_queue.h"

namespace quicx {
namespace quic {

/*
 multi processor, supports multiple threads
*/
class ConnectionTransfor;
class MultiProcessor:
    public Processor,
    public common::ThreadWithQueue<std::function<void()>> {
public:
    MultiProcessor(std::shared_ptr<TLSCtx> ctx,
        std::function<void(std::shared_ptr<IConnection>)> connection_handler);
    virtual ~MultiProcessor();

    virtual void Run();

    virtual void Stop();

    void Weakeup();
    
protected:
    virtual bool HandlePacket(std::shared_ptr<INetPacket> packet);

    // transfer a connection from other processor
    void TransferConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn);
    // all threads can't find the connection
    void ConnectionIDNoexist(uint64_t cid_hash, std::shared_ptr<IConnection>& conn);
    // catch a connection from local map if there is target conn
    void CatchConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn);

private:
    friend class ConnectionTransfor;
    std::shared_ptr<ConnectionTransfor> connection_transfor_;

    static std::unordered_map<std::thread::id, MultiProcessor*> processor_map__;
};

}
}

#endif