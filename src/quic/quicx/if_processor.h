#ifndef QUIC_QUICX_IF_PROCESSOR
#define QUIC_QUICX_IF_PROCESSOR

#include "common/thread/thread.h"

namespace quicx {
namespace quic {

class IProcessor:
    public common::Thread {
public:
    IProcessor() {}
    virtual ~IProcessor() {}

    std::thread::id GetThreadId() const { return std::this_thread::get_id(); }
};

}
}

#endif