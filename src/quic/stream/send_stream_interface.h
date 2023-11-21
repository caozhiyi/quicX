#ifndef QUIC_STREAM_SEND_STREAM_INTERFACE
#define QUIC_STREAM_SEND_STREAM_INTERFACE

#include "quic/stream/stream_interface.h"
#include "quic/stream/send_state_machine.h"

namespace quicx {
namespace quic {

class ISendStream:
    public virtual IStream {
public:
    ISendStream(uint64_t id = 0, bool is_crypto_stream = false);
    virtual ~ISendStream();

    // send data to peer
    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;

    // reset the stream
    virtual void Reset(uint64_t err) = 0;

    typedef std::function<void(uint32_t/*sended size*/, int32_t/*error no*/)> StreamSendedCB;
    virtual void SetSendedCB(StreamSendedCB cb) { _sended_cb = cb; }

protected:
    bool _is_crypto_stream;
    StreamSendedCB _sended_cb;
    std::shared_ptr<SendStreamStateMachine> _send_machine;
};

}
}

#endif
