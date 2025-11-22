#ifndef HTTP3_STREAM_UNIDENTIFIED_STREAM
#define HTTP3_STREAM_UNIDENTIFIED_STREAM

#include <memory>
#include <functional>
#include <vector>

#include "http3/stream/if_recv_stream.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief UnidentifiedStream reads the stream type from the first byte(s) of a unidirectional stream
 * 
 * According to RFC 9114 Section 6.2:
 * "Each unidirectional stream of a certain type begins with a stream type, 
 * sent as a variable-length integer."
 * 
 * Stream types:
 * - 0x00: Control Stream (RFC 9114 Section 6.2.1)
 * - 0x01: Push Stream (RFC 9114 Section 4.6)
 * - 0x02: QPACK Encoder Stream (RFC 9204 Section 4.2)
 * - 0x03: QPACK Decoder Stream (RFC 9204 Section 4.2)
 * 
 * This class is temporary and will be replaced once the stream type is identified.
 */
class UnidentifiedStream:
    public IRecvStream {
public:
    using StreamTypeCallback = std::function<void(
        uint64_t stream_type, 
        std::shared_ptr<IQuicRecvStream> stream,
        std::shared_ptr<IBufferRead> data)>;
    /**
     * @brief Construct an UnidentifiedStream
     * @param stream The QUIC recv stream
     * @param error_handler Error callback
     * @param type_callback Callback invoked when stream type is identified
     */
    UnidentifiedStream(
        const std::shared_ptr<IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const StreamTypeCallback& type_callback);
    
    virtual ~UnidentifiedStream() {}

private:
    void OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) override;

private:
    bool type_identified_;
    StreamTypeCallback type_callback_;
    
};

}
}

#endif

