#ifndef QUIC_STREAM_IF_STREAM
#define QUIC_STREAM_IF_STREAM

#include <list>
#include <memory>
#include <cstdint>
#include <functional>
#include <string>

#include "quic/frame/if_frame.h"
#include "quic/stream/if_frame_visitor.h"
#include "quic/include/if_quic_stream.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace quic {

class IStream:
    public virtual IQuicStream,
    public std::enable_shared_from_this<IStream> {
public:
    IStream(std::shared_ptr<common::IEventLoop> loop,
        uint64_t stream_id,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
        event_loop_(loop),
        stream_id_(stream_id),
        active_send_cb_(active_send_cb),
        stream_close_cb_(stream_close_cb),
        connection_close_cb_(connection_close_cb) {}
    virtual ~IStream();
    // process recv frames
    // return stream data size
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) = 0;

    // try generate data to send
    enum class TrySendResult {
        kSuccess  = 0, // generate data done
        kFailed   = 1, // generate data failed
        kBreak    = 2, // generate data need send alone
    };

    virtual TrySendResult TrySendData(IFrameVisitor* visitor);
    
protected:
    void ToClose();
    void ToSend();
    
protected:
    void* user_data_;

    uint64_t stream_id_;
    // is already active to send?
    bool is_active_send_;
    // frames that wait for sending
    std::list<std::shared_ptr<IFrame>> frames_list_;

    // stream close call back
    std::function<void(uint64_t stream_id)> stream_close_cb_;
    // put stream to active send list callback
    std::function<void(std::shared_ptr<IStream> stream)> active_send_cb_;
    // inner connection close callback
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb_;
    
    std::shared_ptr<common::IEventLoop> event_loop_;
};

}
}

#endif
