#ifndef QUIC_STREAM_SEND_STREAM_INTERFACE
#define QUIC_STREAM_SEND_STREAM_INTERFACE

#include "quic/stream/stream_interface.h"
#include "quic/stream/state_machine_interface.h"

namespace quicx {

typedef std::function<void(uint32_t size, int32_t err)> StreamSendCB;

class SendStreamVisitor {
public:
    SendStreamVisitor() {}
    virtual ~SendStreamVisitor() {}

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) = 0;
};

class ISendStream:
    public virtual IStream {
public:
    ISendStream(uint64_t id = 0);
    virtual ~ISendStream();

    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;

    // reset the stream
    virtual void Reset(uint64_t err) = 0;

    virtual void HandleFrame(SendStreamVisitor& visitior) = 0;

    void SetWriteCallBack(StreamSendCB cb) { _write_cb = cb; }

protected:
    StreamSendCB _write_cb;
    std::shared_ptr<IStreamStateMachine> _send_machine;
};

}

#endif
