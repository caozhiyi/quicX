#ifndef HTTP3_STREAM_IF_RECV_STREAM
#define HTTP3_STREAM_IF_RECV_STREAM

#include "http3/stream/if_stream.h"
#include "common/buffer/if_buffer_read.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief IRecvStream is the base class for all HTTP/3 recv streams
 * 
 * All HTTP/3 recv streams inherit from this class.
 * 
 * The recv stream is used to receive data from the peer.
 * It is responsible for receiving the data from the peer and calling the callback function.
 */
class IRecvStream:
    public IStream {
public:
    IRecvStream(StreamType stream_type,
                const std::shared_ptr<quic::IQuicRecvStream>& stream,
                const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
                IStream(stream_type, error_handler),
                stream_(stream) {}
    virtual ~IRecvStream() {}

    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }
    
    // when there are some data received, the callback function will be called.
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) = 0;

protected:
    std::shared_ptr<quic::IQuicRecvStream> stream_;
};

}
}

#endif
