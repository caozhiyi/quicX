#ifndef QUIC_QUICX_IF_PROCESSOR
#define QUIC_QUICX_IF_PROCESSOR

namespace quicx {
namespace quic {

class IProcessor {
public:
    IProcessor() {}
    virtual ~IProcessor() {}

    virtual void start() = 0;
};

}
}

#endif