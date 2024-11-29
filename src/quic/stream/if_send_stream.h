#ifndef QUIC_STREAM_IF_SEND_STREAM
#define QUIC_STREAM_IF_SEND_STREAM

#include "quic/stream/if_stream.h"
#include "quic/stream/state_machine_send.h"

namespace quicx {
namespace quic {

class ISendStream:
    public virtual IStream {
public:
    ISendStream(uint64_t id = 0, bool is_crypto_stream = false);
    virtual ~ISendStream();

    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;

    virtual void SetSendedCB(std::function<void(uint32_t/*sended size*/, int32_t/*error no*/)> cb)
        { sended_cb_ = cb; }

protected:
    bool is_crypto_stream_;
    std::shared_ptr<StreamStateMachineSend> send_machine_;

    std::function<void(uint32_t/*sended size*/, int32_t/*error no*/)> sended_cb_;
};

}
}

#endif
