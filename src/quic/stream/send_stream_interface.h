#ifndef QUIC_STREAM_SEND_STREAM_INTERFACE
#define QUIC_STREAM_SEND_STREAM_INTERFACE

#include "quic/stream/stream_interface.h"
#include "quic/stream/state_machine_interface.h"

namespace quicx {
class ISendStream;

typedef std::function<void(uint32_t size, int32_t err)> StreamSendCB;
typedef std::function<void(ISendStream* stream)> StreamHopeSendCB;

class ISendStream:
    public virtual IStream {
public:
    ISendStream(uint64_t id = 0, bool is_crypto_stream = false);
    virtual ~ISendStream();

    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;

    // reset the stream
    virtual void Reset(uint64_t err) = 0;

    void SetSendCB(StreamSendCB cb) { _send_cb = cb; }
    void SetHopeSendCB(StreamHopeSendCB cb) { _hope_send_cb = cb; }

protected:
    bool _is_crypto_stream;
    StreamSendCB _send_cb;
    StreamHopeSendCB _hope_send_cb;
    std::shared_ptr<IStreamStateMachine> _send_machine;
};

}

#endif
