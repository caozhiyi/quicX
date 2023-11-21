#ifndef QUIC_STREAM_RECV_STREAM_INTERFACE
#define QUIC_STREAM_RECV_STREAM_INTERFACE

#include "quic/stream/stream_interface.h"
#include "quic/stream/recv_state_machine.h"
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {
namespace quic {

class IRecvStream:
    public virtual IStream {
public:
    IRecvStream(uint64_t id = 0): IStream(id), _recv_cb(nullptr) {}
    virtual ~IRecvStream() {}

    typedef std::function<void(std::shared_ptr<common::IBufferChains>/*recv buffer*/, int32_t /*error no*/)> StreamRecvCB;
    void SetRecvCallBack(StreamRecvCB rb) { _recv_cb = rb; }

protected:
    StreamRecvCB _recv_cb;
    std::shared_ptr<RecvStreamStateMachine> _recv_machine;
};

}
}

#endif
