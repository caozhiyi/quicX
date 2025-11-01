#ifndef HTTP3_STREAM_IF_RECV_STREAM
#define HTTP3_STREAM_IF_RECV_STREAM

#include "http3/stream/if_stream.h"
#include "common/buffer/if_buffer_read.h"

namespace quicx {
namespace http3 {

class IRecvStream:
    public IStream {
public:
    IRecvStream(const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler): IStream(error_handler) {}
    virtual ~IRecvStream() {}

    // when there are some data received, the callback function will be called.
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) = 0;
};

}
}

#endif
