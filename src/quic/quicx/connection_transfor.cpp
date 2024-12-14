#include "quic/quicx/multi_processor.h"
#include "quic/quicx/connection_transfor.h"

namespace quicx {
namespace quic {

ConnectionTransfor::ConnectionTransfor() {
    thread_id_ = std::this_thread::get_id();
}

ConnectionTransfor::~ConnectionTransfor() {

}

void ConnectionTransfor::TryCatchConnection(uint64_t cid_hash) {
    // make a new searching context, so we can support multi searching at the same time
    std::shared_ptr<SearchingContext> context = std::make_shared<SearchingContext>();
    context->cid_hash_ = cid_hash;
    context->thread_id_ = thread_id_;
    context->count_ = MultiProcessor::processor_map__.size() - 1;

    for (auto iter = MultiProcessor::processor_map__.begin(); iter != MultiProcessor::processor_map__.end(); iter++) {
        if (iter->first == thread_id_) {
            continue;
        }
        iter->second->Push([iter, context, this]()->void{
            // if found the connection, current thread does not need to search
            if (context->count_.load() <= 0){
                return;
            }
                
            iter->second->CatchConnection(context->cid_hash_, context->connection_);
            // found the connection, so the source processor can process the connection
            if (context->connection_ != nullptr) {
                context->count_.exchange(0);
                this->ExistConnection(context);

            } else {
                if (context->count_.fetch_sub(1) <= 0) {
                    this->NoExistConnection(context);
                }
            }
        });
        iter->second->Weakeup();
    }
}

void ConnectionTransfor::ExistConnection(std::shared_ptr<SearchingContext> context) {
    auto iter = MultiProcessor::processor_map__.find(thread_id_);
    iter->second->Push([iter, context]()->void{
        iter->second->CatchConnection(context->cid_hash_, context->connection_);
    });
    iter->second->Weakeup();
}

void ConnectionTransfor::NoExistConnection(std::shared_ptr<SearchingContext> context) {
    auto iter = MultiProcessor::processor_map__.find(thread_id_);
    iter->second->Push([iter, context]()->void{
        iter->second->ConnectionIDNoexist();
    });
    iter->second->Weakeup();
}

}
}
