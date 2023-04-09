#ifndef QUIC_STREAM_STREAM_INTERFACE
#define QUIC_STREAM_STREAM_INTERFACE

#include <memory>
#include <cstdint>
#include <functional>

#include "quic/stream/type.h"
#include "quic/common/send_data_visitor.h"

namespace quicx {

class IFrame;
class IStream {
public:
    IStream(uint64_t id = 0): _stream_id(id) {}
    virtual ~IStream() {}

    virtual void Close() = 0;

    virtual bool TrySendData(SendDataVisitor& visitior) = 0;

    virtual void OnFrame(std::shared_ptr<IFrame> frame) = 0;

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

protected:
    uint64_t _stream_id;
};

}

#endif
