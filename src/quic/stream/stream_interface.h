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

enum TrySendResult {
    TSR_SUCCESS = 0,
    TSR_FAILED  = 1,
    TSR_BREAK   = 2
};

class IStream {
public:
    IStream(uint64_t id = 0): _stream_id(id) {}
    virtual ~IStream() {}

    virtual void Close() = 0;

    virtual TrySendResult TrySendData(IFrameVisitor* visitor) = 0;

    virtual void OnFrame(std::shared_ptr<IFrame> frame) = 0;

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

protected:
    uint64_t _stream_id;
    // frames that wait for sending
    std::list<std::shared_ptr<IFrame>> _frame_list;

};

}

#endif
