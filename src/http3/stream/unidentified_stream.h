#ifndef HTTP3_STREAM_UNIDENTIFIED_STREAM
#define HTTP3_STREAM_UNIDENTIFIED_STREAM

#include <memory>
#include <functional>
#include <vector>

#include "http3/stream/if_stream.h"
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
class UnidentifiedStream : public IStream {
public:
    using StreamTypeCallback = std::function<void(
        uint64_t stream_type, 
        std::shared_ptr<quic::IQuicRecvStream> stream,
        const std::vector<uint8_t>& remaining_data)>;
    
    /**
     * @brief Construct an UnidentifiedStream
     * @param stream The QUIC recv stream
     * @param error_handler Error callback
     * @param type_callback Callback invoked when stream type is identified
     */
    UnidentifiedStream(
        const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const StreamTypeCallback& type_callback);
    
    virtual ~UnidentifiedStream();

    // IStream interface
    virtual StreamType GetType() override { return StreamType::kReqResp; } // Temporary, not applicable
    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

private:
    void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error);
    bool TryReadStreamType();

private:
    std::shared_ptr<quic::IQuicRecvStream> stream_;
    StreamTypeCallback type_callback_;
    std::vector<uint8_t> buffer_;  // Buffer for incomplete varint
    bool type_identified_;
};

}
}

#endif

