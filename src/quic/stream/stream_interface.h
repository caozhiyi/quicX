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
namespace quic {

class IStream:
    public std::enable_shared_from_this<IStream> {
public:
    IStream(uint64_t id = 0): _stream_id(id), _is_active_send(0) {}
    virtual ~IStream() {}

    // close the stream
    virtual void Close(uint64_t error = 0) = 0;

    // process recv frames
    // return stream data size
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) = 0;

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

    // try generate data to send
    enum TrySendResult {
        TSR_SUCCESS = 0, // generate data done
        TSR_FAILED  = 1, // generate data failed
        TSR_BREAK   = 2  // generate data need send alone
    };
    virtual TrySendResult TrySendData(IFrameVisitor* visitor);

    typedef std::function<void(std::shared_ptr<IStream> /*stream*/)> ActiveStreamSendCB;
    void SetActiveStreamSendCB(ActiveStreamSendCB cb) { _active_send_cb = cb; }

    typedef std::function<void(uint64_t/*errro*/, uint16_t/*tigger frame*/, const std::string&/*resion*/)> ConnectionCloseCB;
    void SetConnectionCloseCB(ConnectionCloseCB cb) { _connection_close_cb = cb; }

    typedef std::function<void(uint64_t/*stream id*/)> StreamCloseCB;
    void SetStreamCloseCB(StreamCloseCB cb) { _stream_close_cb = cb; }
    

    void NoticeToClose();
    void ActiveToSend();
    

protected:
    uint64_t _stream_id;
    bool _is_active_send;
    // put stream to active send list callback
    ActiveStreamSendCB _active_send_cb;
    // inner connection close callback
    ConnectionCloseCB _connection_close_cb;
    // stream close call back
    StreamCloseCB _stream_close_cb;
    // frames that wait for sending
    std::list<std::shared_ptr<IFrame>> _frames_list;
};

}
}

#endif
