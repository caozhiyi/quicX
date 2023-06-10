#ifndef QUIC_STREAM_STREAM_INTERFACE
#define QUIC_STREAM_STREAM_INTERFACE

#include <list>
#include <memory>
#include <cstdint>
#include <functional>

#include "quic/stream/type.h"
#include "quic/frame/frame_interface.h"
#include "quic/stream/frame_visitor_interface.h"

namespace quicx {

class IStream {
public:
    IStream(uint64_t id = 0): _stream_id(id), _is_active_send(0) {}
    virtual ~IStream() {}

    // close the stream
    virtual void Close(uint64_t error = 0) = 0;

    // process recv frames
    virtual void OnFrame(std::shared_ptr<IFrame> frame) = 0;

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

    // try generate data to send
    enum TrySendResult {
        TSR_SUCCESS = 0, // generate data done
        TSR_FAILED  = 1, // generate data failed
        TSR_BREAK   = 2  // generate data need send alone
    };
    virtual TrySendResult TrySendData(IFrameVisitor* visitor);

    typedef std::function<void(IStream* /*stream*/)> ActiveStreamSendCB;
    void SetHopeSendCB(ActiveStreamSendCB cb) { _active_send_cb = cb; }
    
protected:
    void ActiveToSend();

protected:
    uint64_t _stream_id;
    bool _is_active_send;
    // put stream to active send list callback
    ActiveStreamSendCB _active_send_cb;
    // frames that wait for sending
    std::list<std::shared_ptr<IFrame>> _frame_list;

};

}

#endif
