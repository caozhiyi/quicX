#include <thread>
#include "common/log/log.h"
#include "quic/app/server.h"
#include "quic/process/server_processor.h"

namespace quicx {

Server::Server(RunThreadType run_type):
    IApplication(run_type) {

}

Server::~Server() {

}

bool Server::Listen(const std::string& ip, uint16_t port) {
    return _receiver->Listen(ip, port);
}

void Server::MainLoop(uint16_t thread_num) {
    if (thread_num < 1) {
        thread_num = 1;
    }
    if (thread_num > 10) {
        thread_num = 10;
    }

    _processors.resize(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ServerProcessor>();
        processor->SetRecvFunction([this] { return _receiver->DoRecv(); });
        _processors.emplace_back(processor);
    }
    IApplication::MainLoop(thread_num);
}

}