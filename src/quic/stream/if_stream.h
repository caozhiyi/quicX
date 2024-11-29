#ifndef QUIC_STREAM_IF_STREAM
#define QUIC_STREAM_IF_STREAM

#include <list>
#include <memory>
#include <cstdint>
#include <functional>

#include "quic/stream/type.h"
#include "quic/frame/frame_interface.h"
#include "quic/stream/if_frame_visitor.h"

namespace quicx {
namespace quic {

class IStream:
    public std::enable_shared_from_this<IStream> {
public:
    IStream(uint64_t id = 0): stream_id_(id), is_active_send_(0) {}
    virtual ~IStream() {}

    // close the stream
    virtual void Close() {}

    // reset the stream
    virtual void Reset(uint64_t err) = 0;

    // process recv frames
    // return stream data size
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) = 0;

    void SetStreamID(uint64_t id) { stream_id_ = id; }
    uint64_t GetStreamID() { return stream_id_; }

    // try generate data to send
    enum TrySendResult {
        TSR_SUCCESS = 0, // generate data done
        TSR_FAILED  = 1, // generate data failed
        TSR_BREAK   = 2  // generate data need send alone
    };
    virtual TrySendResult TrySendData(IFrameVisitor* visitor);

    void SetActiveStreamSendCB(std::function<void(std::shared_ptr<IStream> /*stream*/)> cb)
        { active_send_cb_ = cb; }

    void SetConnectionCloseCB(std::function<void(uint64_t/*errro*/, uint16_t/*tigger frame*/, const std::string&/*resion*/)> cb)
        { connection_close_cb_ = cb; }

    void SetStreamCloseCB(std::function<void(uint64_t/*stream id*/)> cb)
        { stream_close_cb_ = cb; }
    
protected:
    void ToClose();
    void ToSend();
    
protected:
    uint64_t stream_id_;
    // is already active to send?
    bool is_active_send_;
    // frames that wait for sending
    std::list<std::shared_ptr<IFrame>> frames_list_;

    // stream close call back
    std::function<void(uint64_t/*stream id*/)> stream_close_cb_;
    // put stream to active send list callback
    std::function<void(std::shared_ptr<IStream> /*stream*/)> active_send_cb_;
    // inner connection close callback
    std::function<void(uint64_t/*errro*/, uint16_t/*tigger frame*/, const std::string&/*resion*/)> connection_close_cb_;
    
};

}
}

#endif
