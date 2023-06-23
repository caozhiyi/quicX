#include <thread>
#include "common/log/log.h"
#include "quic/app/app_interface.h"
#include "quic/process/thread_receiver.h"

namespace quicx {

IApplication::IApplication(RunThreadType run_type):
    _run_thread_type(run_type) {
    _ctx = std::make_shared<TLSCtx>();
    if (!_ctx->Init()) {
        LOG_ERROR("tls ctx init faliled.");
    }

    if (run_type == RTT_SINGLE) {
        _receiver = std::make_shared<Receiver>();

    } else {
        _receiver = std::make_shared<ThreadReceiver>();
    }
}

IApplication::~IApplication() {

}

void IApplication::MainLoop(uint16_t thread_num) {
    if (_run_thread_type == RTT_SINGLE) {
        _processors[0]->MainLoop();
        return;
    }
    
    std::vector<std::thread> threads;
    threads.resize(thread_num);

    for (size_t i = 0; i < thread_num; i++) {
        threads.emplace_back(std::thread([this, i] { _processors[i]->MainLoop(); }));
    }
    
    for (size_t i = 0; i < thread_num; i++) {
        threads[i].join();
    }
}

}