#ifndef QUIC_APP_INTERFACE
#define QUIC_APP_INTERFACE

#include <memory>
#include "quic/app/type.h"
#include "quic/process/receiver.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/process/processor_interface.h"

namespace quicx {

class IApplication {
public:
    IApplication(RunThreadType run_type = RTT_SINGLE);
    virtual ~IApplication();

    void MainLoop(uint16_t thread_num = 1);

protected:
    std::shared_ptr<TLSCtx> _ctx;
    RunThreadType _run_thread_type;
    std::shared_ptr<Receiver> _receiver;
    std::vector<std::shared_ptr<IProcessor>> _processors;
};

}

#endif