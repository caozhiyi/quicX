#include "common/log/log.h"
#include "quic/quicx/receiver.h"
#include "quic/quicx/processor.h"
#include "quic/quicx/quicx_impl.h"
#include "quic/quicx/thread_receiver.h"
#include "quic/connection/client_connection.h"

namespace quicx {

QuicxImpl::QuicxImpl() {

}

QuicxImpl::~QuicxImpl() {

}

bool QuicxImpl::Init(uint16_t thread_num) {
    _ctx = std::make_shared<TLSCtx>();
    if (!_ctx->Init()) {
        LOG_ERROR("tls ctx init faliled.");
        return false;
    }

    if (thread_num == 1) {
        _receiver = std::make_shared<Receiver>();

    } else {
        _receiver = std::make_shared<ThreadReceiver>();
    }

    _processors.resize(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<Processor>();
        processor->SetRecvFunction([this] { return _receiver->DoRecv(); });
        processor->SetAddConnectionIDCB([this] (uint64_t id) { _receiver->RegisteConnection(std::this_thread::get_id(), id);});
        processor->SetRetireConnectionIDCB([this] (uint64_t id) { _receiver->CancelConnection(std::this_thread::get_id(), id);});
        processor->Start();
        _processors.emplace_back(processor);
    }
    return true;
}

void QuicxImpl::Join() {
    for (size_t i = 0; i < _processors.size(); i++) {
        _processors[i]->Join();
    }
    
}

void QuicxImpl::Destroy() {
    if (_receiver) {
        auto receiver = std::dynamic_pointer_cast<ThreadReceiver>(_receiver);
        if (receiver) {
            receiver->Stop();
            receiver->WeakUp();
        }
    }

    for (size_t i = 0; i < _processors.size(); i++) {
        _processors[i]->Stop();
        _processors[i]->WeakUp();
    }
}

bool QuicxImpl::Connection(const std::string& ip, uint16_t port) {
    if (!_processors.empty()) {
        // TODO 
        // 1. random select processor
        // 2. client connectin manage
        auto conn = _processors[0]->MakeClientConnection();
        auto cli_conn = std::dynamic_pointer_cast<ClientConnection>(conn);
        Address addr(AT_IPV4, ip, port);
        if (cli_conn->Dial(addr)) {
            _receiver->SetRecvSocket(cli_conn->GetSock());
            return true;
        }
    }
    return false;
}

bool QuicxImpl::ListenAndAccept(const std::string& ip, uint16_t port) {
    if (_receiver) {
        return _receiver->Listen(ip, port);
    }
    return false;
}

}