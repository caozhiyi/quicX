#ifndef QUIC_QUICX_CONNECTION_TRANSFOR
#define QUIC_QUICX_CONNECTION_TRANSFOR

#include <memory>
#include <thread>
#include <atomic>
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

class ConnectionTransfor {
public:
    ConnectionTransfor();
    virtual ~ConnectionTransfor();

    void TryCatchConnection(uint64_t cid_hash);

private:
    struct SearchingContext {
        uint64_t cid_hash_; // which connection is searched
        std::thread::id thread_id_; // which thread is searching
        std::atomic_int16_t count_; // how many threads
        std::shared_ptr<IConnection> connection_; // if found, this is the connection
    };
    
    void ExistConnection(std::shared_ptr<SearchingContext> context);
    void NoExistConnection(std::shared_ptr<SearchingContext> context);

private:
    std::thread::id thread_id_;
};

}
}

#endif