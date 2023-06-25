#include <thread>
#include "common/log/log.h"
#include "quic/app/client.h"
#include "quic/process/thread_receiver.h"
#include "quic/process/client_processor.h"

namespace quicx {

Client::Client(RunThreadType run_type):
    IApplication(run_type) {
}

Client::~Client() {

}

std::shared_ptr<ClientConnection> Client::Dail(const std::string& ip, uint16_t port) {
    std::shared_ptr<ClientConnection> conn = std::make_shared<ClientConnection>(_ctx);

    Address addr(AT_IPV4, ip, port);
    if (!conn->Dial(addr)) {
        LOG_ERROR("dial addr failed.");
        return nullptr;
    }

    _receiver->SetRecvSocket(conn->GetSock());
    return conn;
}

void Client::MainLoop(uint16_t thread_num) {
    if (thread_num < 1) {
        thread_num = 1;
    }
    if (thread_num > 10) {
        thread_num = 10;
    }

    _processors.resize(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ClientProcessor>();
        processor->SetRecvFunction([this] { return _receiver->DoRecv(); });
        _processors.emplace_back(processor);
    }
    IApplication::MainLoop(thread_num);
}

}