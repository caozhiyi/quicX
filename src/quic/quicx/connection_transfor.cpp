#include "quic/quicx/processor_base.h"
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
    context->count_ = ProcessorBase::s_processor_map.size() - 1;

    for (auto iter = ProcessorBase::s_processor_map.begin(); iter != ProcessorBase::s_processor_map.end(); iter++) {
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
        iter->second->Wakeup();
    }
}

void ConnectionTransfor::ExistConnection(std::shared_ptr<SearchingContext> context) {
    auto iter = ProcessorBase::s_processor_map.find(context->thread_id_);
    iter->second->Push([iter, context]()->void{
        iter->second->TransferConnection(context->cid_hash_, context->connection_);
    });
    iter->second->Wakeup();
}

void ConnectionTransfor::NoExistConnection(std::shared_ptr<SearchingContext> context) {
    auto iter = ProcessorBase::s_processor_map.find(context->thread_id_);
    iter->second->Push([iter, context]()->void{
        iter->second->ConnectionIDNoexist(context->cid_hash_, context->connection_);
    });
    iter->second->Wakeup();
}

}
}
