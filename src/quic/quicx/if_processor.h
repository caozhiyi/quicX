#ifndef QUIC_QUICX_IF_PROCESSOR
#define QUIC_QUICX_IF_PROCESSOR

#include "common/thread/thread.h"

namespace quicx {
namespace quic {

class IProcessor {
public:
    IProcessor() {}
    virtual ~IProcessor() {}

    // does the processing
    virtual void Process() = 0;

    virtual void AddReceiver(uint64_t socket_fd) = 0;
    virtual void AddReceiver(const std::string& ip, uint16_t port) = 0;
};

}
}

#endif