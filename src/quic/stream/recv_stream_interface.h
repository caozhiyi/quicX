#ifndef QUIC_STREAM_RECV_STREAM_INTERFACE
#define QUIC_STREAM_RECV_STREAM_INTERFACE

#include "quic/stream/stream_interface.h"
#include "quic/stream/state_machine_interface.h"
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {

typedef std::function<void(std::shared_ptr<IBufferChains> buffer, int32_t err)> StreamRecvCB;

class IRecvStream:
    public virtual IStream {
public:
    IRecvStream(uint64_t id = 0): IStream(id), _recv_cb(nullptr) {}
    virtual ~IRecvStream() {}

    virtual void OnFrame(std::shared_ptr<IFrame> frame) = 0;

    void SetRecvCallBack(StreamRecvCB rb) { _recv_cb = rb; }

protected:
    StreamRecvCB _recv_cb;
    std::shared_ptr<IStreamStateMachine> _recv_machine;
};

}

#endif
