#ifndef QUIC_STREAM_IF_RECV_STREAM
#define QUIC_STREAM_IF_RECV_STREAM

#include "quic/stream/if_stream.h"
#include "quic/stream/state_machine_recv.h"
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {
namespace quic {

class IRecvStream:
    public virtual IStream {
public:
    IRecvStream(uint64_t id = 0);
    virtual ~IRecvStream();

    void SetRecvCallBack(std::function<void(std::shared_ptr<common::IBufferChains>/*recv buffer*/, int32_t /*error no*/)> cb)
        { recv_cb_ = cb; }

protected:
    std::shared_ptr<StreamStateMachineRecv> recv_machine_;
    std::function<void(std::shared_ptr<common::IBufferChains>/*recv buffer*/, int32_t /*error no*/)> recv_cb_;
};

}
}

#endif
